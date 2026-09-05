// SPDX-License-Identifier: GPL-3.0-or-later
// FindBar.cpp — see FindBar.h for why this is modeless and what it deliberately does not own.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/FindBar.h"

#include "core/Logger.h"
#include "editor/TextSearch.h"   // matchAtOrAfter / matchBefore — the wrap lives there
#include "host/win32/D2DEditor.h"
#include "host/win32/Theme.h"

#include <windows.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>

namespace sentinelide {
namespace {

// 96-dpi design metrics. The find field is the only elastic one: everything else has a
// label in it whose width is a property of the text, not of the window.
constexpr int kPadX = 10, kPadY = 6, kRowH = 24, kRowGap = 6, kGap = 8;
constexpr int kCountW = 108, kPrevW = 64, kNextW = 56, kCaseW = 98, kWordW = 106, kCloseW = 26;
constexpr int kReplaceW = 78, kReplaceAllW = 92;
constexpr int kFieldMinW = 110, kFieldMaxW = 260;

struct FindState {
    HWND hwnd = nullptr, parent = nullptr, editor = nullptr;
    HWND eFind = nullptr, eRepl = nullptr, sCount = nullptr;
    HWND bPrev = nullptr, bNext = nullptr, bClose = nullptr;
    HWND kCase = nullptr, kWord = nullptr;
    HWND bReplace = nullptr, bReplaceAll = nullptr;
    HFONT ui = nullptr;
    UINT dpi = 96;
    bool replaceMode = false;

    // Is the bar OPEN? Tracked here rather than asked of IsWindowVisible, and the
    // difference is not academic: IsWindowVisible is false whenever any ANCESTOR is
    // hidden, so it answers "is this on screen" when the question is "has the user opened
    // find". It reported closed during the main window's WM_CREATE — where layout() runs,
    // and would therefore have given the band no height — and it reported closed for the
    // whole of tests/d2d_dialect_test.cpp, whose host window is deliberately never shown,
    // which silently turned hideFindBar into a no-op there.
    bool open = false;

    std::vector<editor::Range> matches;
    size_t current = 0;
    bool haveCurrent = false;

    // Where an as-you-type search starts. It is NOT the live caret: while the user types
    // into the find field the editor's selection is being moved by us, one match per
    // keystroke, so reading the caret back would make the search chase its own tail and
    // walk forward through the file as characters are added. It is the caret as it was
    // when the bar opened, and it is re-pinned each time the user deliberately moves —
    // Next, Previous, or a Replace.
    size_t origin = 0;

    // A one-shot line that replaces the count readout ("Replaced 27"). Cleared by the next
    // thing the user does in the bar, so it never becomes a stale claim.
    std::wstring note;

    // >0 while THIS bar is the thing editing the buffer. d2dEditorReplaceRanges raises
    // EN_CHANGE synchronously and the host routes that straight back here as
    // findBarBufferChanged, so without this a Replace All would run the search twice: once
    // re-entrantly on the new buffer, and once when we get control back. Correct either
    // way — but on a large file that is a second full scan for nothing.
    int inEdit = 0;
};

FindState* state(HWND h) {
    return reinterpret_cast<FindState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

int dp(FindState* st, int v) { return MulDiv(v, static_cast<int>(st->dpi), 96); }

int barHeightFor(FindState* st) {
    return st->replaceMode ? dp(st, kPadY + kRowH + kRowGap + kRowH + kPadY)
                           : dp(st, kPadY + kRowH + kPadY);
}

bool checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

std::wstring textOf(HWND h) {
    const int n = GetWindowTextLengthW(h);
    if (n <= 0) return std::wstring();
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    const int got = GetWindowTextW(h, s.data(), n + 1);
    s.resize(got > 0 ? static_cast<size_t>(got) : 0);
    return s;
}

// ---- geometry ---------------------------------------------------------------

void placeChildren(FindState* st) {
    RECT rc{};
    GetClientRect(st->hwnd, &rc);
    const int W = rc.right;
    const int padX = dp(st, kPadX), padY = dp(st, kPadY), rowH = dp(st, kRowH);
    const int gap = dp(st, kGap), rowGap = dp(st, kRowGap);

    // The find row's fixed furniture, so what is left over is the field.
    const int fixed = dp(st, kCountW + kPrevW + kNextW + kCaseW + kWordW + kCloseW) + 6 * gap;
    int fieldW = W - 2 * padX - fixed;
    fieldW = (std::max)(dp(st, kFieldMinW), (std::min)(dp(st, kFieldMaxW), fieldW));

    int x = padX, y = padY;
    auto put = [&](HWND h, int w, int dy, int h2) {
        if (h) MoveWindow(h, x, y + dy, w, h2, TRUE);
        x += w + gap;
    };
    put(st->eFind, fieldW, 0, rowH);
    put(st->sCount, dp(st, kCountW), dp(st, 4), rowH);
    put(st->bPrev, dp(st, kPrevW), 0, rowH);
    put(st->bNext, dp(st, kNextW), 0, rowH);
    put(st->kCase, dp(st, kCaseW), dp(st, 3), rowH);
    put(st->kWord, dp(st, kWordW), dp(st, 3), rowH);
    // The close box is right-aligned, not next in the flow: it is the one control whose
    // position should not move when the window is resized or a label changes width.
    if (st->bClose) MoveWindow(st->bClose, W - padX - dp(st, kCloseW), y, dp(st, kCloseW), rowH, TRUE);

    x = padX;
    y = padY + rowH + rowGap;
    put(st->eRepl, fieldW, 0, rowH);
    put(st->bReplace, dp(st, kReplaceW), 0, rowH);
    put(st->bReplaceAll, dp(st, kReplaceAllW), 0, rowH);
}

void showReplaceRow(FindState* st, bool on) {
    const int cmd = on ? SW_SHOW : SW_HIDE;
    ShowWindow(st->eRepl, cmd);
    ShowWindow(st->bReplace, cmd);
    ShowWindow(st->bReplaceAll, cmd);
}

// ---- the search itself ------------------------------------------------------

void updateCount(FindState* st) {
    std::wstring s = st->note;
    if (s.empty()) {
        if (textOf(st->eFind).empty())
            s.clear();
        else if (st->matches.empty())
            s = L"No results";
        else
            s = std::to_wstring(st->haveCurrent ? st->current + 1 : 0) + L" of " +
                std::to_wstring(st->matches.size());
    }
    SetWindowTextW(st->sCount, s.c_str());
    InvalidateRect(st->sCount, nullptr, TRUE);
}

// Re-run the search over the CURRENT buffer and pick the match to sit on.
//
// `pick` decides where the caret lands afterwards:
//   0  don't move the editor at all (a recount after someone else's edit)
//   +1 select the first match at or after `origin` (typing in the find field, F3)
//   -1 select the last match at or before `origin` (Shift+F3)
void recompute(FindState* st, int pick) {
    const std::wstring needle = textOf(st->eFind);
    d2dEditorFindAll(st->editor, needle, checked(st->kCase), checked(st->kWord), st->matches);

    if (st->matches.empty()) {
        st->haveCurrent = false;
        updateCount(st);
        return;
    }
    if (pick == 0) {
        // Keep pointing at something valid without moving the user: the old index may be
        // past the end of a buffer that just shrank.
        if (st->haveCurrent && st->current >= st->matches.size())
            st->current = st->matches.size() - 1;
        updateCount(st);
        return;
    }
    st->current = (pick > 0) ? editor::matchAtOrAfter(st->matches, st->origin)
                             : editor::matchBefore(st->matches, st->origin);
    st->haveCurrent = true;
    const editor::Range& r = st->matches[st->current];
    d2dEditorSelectRange(st->editor, r.start, r.end);
    updateCount(st);
}

// Next / previous, with the wrap. The origin is re-pinned to the match we land on so the
// following step continues from here rather than from wherever the bar was opened.
void step(FindState* st, int dir) {
    st->note.clear();
    if (textOf(st->eFind).empty()) {
        SetFocus(st->eFind);
        return;
    }
    d2dEditorFindAll(st->editor, textOf(st->eFind), checked(st->kCase), checked(st->kWord),
                     st->matches);
    if (st->matches.empty()) {
        st->haveCurrent = false;
        updateCount(st);
        return;
    }
    if (!st->haveCurrent) {
        // First step of a search: land on the match nearest the caret rather than at the
        // top of the file.
        size_t a = 0, b = 0;
        d2dEditorSelection(st->editor, a, b);
        st->origin = a;
    } else if (dir > 0) {
        // Search from just past the START of the current match, not from its end: a needle
        // that overlaps nothing steps by one either way, but starting from the end would
        // skip a match that begins inside the current one under a future overlapping mode.
        st->origin = st->matches[(std::min)(st->current, st->matches.size() - 1)].start + 1;
    } else {
        st->origin = st->matches[(std::min)(st->current, st->matches.size() - 1)].start;
    }
    st->current = (dir > 0) ? editor::matchAtOrAfter(st->matches, st->origin)
                            : editor::matchBefore(st->matches, st->origin);
    st->haveCurrent = true;
    const editor::Range& r = st->matches[st->current];
    st->origin = r.start;
    d2dEditorSelectRange(st->editor, r.start, r.end);
    updateCount(st);
}

// Replace the current match, then move to the next one. If the editor's selection is not
// sitting exactly on the current match — the user clicked somewhere, or the buffer moved
// under us — this only FINDS, it does not replace. Replacing text the user cannot see
// selected is the one behaviour here that could quietly damage a file.
void replaceCurrent(FindState* st) {
    st->note.clear();
    if (!st->haveCurrent || st->matches.empty()) {
        step(st, +1);
        return;
    }
    const editor::Range r = st->matches[st->current];
    size_t a = 0, b = 0;
    d2dEditorSelection(st->editor, a, b);
    if (a != r.start || b != r.end) {
        step(st, +1);
        return;
    }
    const std::wstring repl = textOf(st->eRepl);
    std::vector<editor::Range> one{ r };
    ++st->inEdit;
    const bool changed = d2dEditorReplaceRanges(st->editor, one, repl);
    --st->inEdit;
    // Carry on from just after what we wrote, so the next match is found even when the
    // replacement itself contains the needle.
    st->origin = changed ? r.start + repl.size() : r.start + 1;
    st->haveCurrent = false;
    recompute(st, +1);
}

void replaceAll(FindState* st) {
    st->note.clear();
    d2dEditorFindAll(st->editor, textOf(st->eFind), checked(st->kCase), checked(st->kWord),
                     st->matches);
    const size_t n = st->matches.size();
    if (n == 0) {
        st->haveCurrent = false;
        updateCount(st);
        return;
    }
    const std::wstring repl = textOf(st->eRepl);
    ++st->inEdit;
    // ONE call, and therefore ONE undo step for all n matches — the whole reason
    // EditorModel::replaceRanges exists rather than a loop here. It also runs the control's
    // afterEdit funnel, so EN_CHANGE fires and g.dirty is recomputed: without that the
    // buffer would be discardable with no prompt.
    const bool changed = d2dEditorReplaceRanges(st->editor, st->matches, repl);
    --st->inEdit;
    logMsg(LogLevel::Info, L"Find: Replace All — " + std::to_wstring(n) +
                               (n == 1 ? L" occurrence" : L" occurrences") +
                               (changed ? L" replaced" : L" left unchanged (identical text)"));
    st->haveCurrent = false;
    st->origin = 0;
    recompute(st, 0);
    st->note = changed ? (L"Replaced " + std::to_wstring(n))
                       : L"No change";
    updateCount(st);
}

// ---- window ------------------------------------------------------------------

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    FindState* st = state(hwnd);
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, w, l);
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const Theme& th = currentTheme();
            FillRect(reinterpret_cast<HDC>(w), &rc, themeBrush(th.panelElevBg));
            // A hairline along the bottom, so the band reads as sitting ON the editor
            // rather than as part of it. The same separator the tab strip and status bar
            // draw, and the same colour token.
            RECT line{ rc.left, rc.bottom - 1, rc.right, rc.bottom };
            FillRect(reinterpret_cast<HDC>(w), &line, themeBrush(th.border));
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            const Theme& th = currentTheme();
            HDC hdc = reinterpret_cast<HDC>(w);
            SetBkColor(hdc, th.panelElevBg);
            // "No results" is the state this feature is most often in while the user is
            // still typing, so it is the one that has to be unmistakable without being
            // alarming — the diagnostic red, on the same band, with no other change.
            const bool none = st && st->matches.empty() && !textOf(st->eFind).empty() &&
                              reinterpret_cast<HWND>(l) == st->sCount && st->note.empty();
            SetTextColor(hdc, none ? th.diagError : th.textSecondary);
            return reinterpret_cast<LRESULT>(themeBrush(th.panelElevBg));
        }
        case WM_CTLCOLORBTN: {
            const Theme& th = currentTheme();
            SetBkColor(reinterpret_cast<HDC>(w), th.panelElevBg);
            SetTextColor(reinterpret_cast<HDC>(w), th.textPrimary);
            return reinterpret_cast<LRESULT>(themeBrush(th.panelElevBg));
        }
        case WM_CTLCOLOREDIT: {
            const Theme& th = currentTheme();
            SetBkColor(reinterpret_cast<HDC>(w), th.windowBg);
            SetTextColor(reinterpret_cast<HDC>(w), th.textPrimary);
            return reinterpret_cast<LRESULT>(themeBrush(th.windowBg));
        }
        case WM_SIZE:
            if (st) placeChildren(st);
            return 0;
        case WM_NCDESTROY:
            // The state is heap-allocated in createFindBar and this is its only teardown.
            // WM_NCDESTROY, not WM_DESTROY: the child controls are destroyed between the
            // two and their WM_CTLCOLOR* still reach this proc, where they read `st`.
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete st;
            return DefWindowProcW(hwnd, msg, w, l);
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(w);
            const int code = HIWORD(w);
            if (id == kFindField && code == EN_CHANGE) {
                // FIND-AS-YOU-TYPE. This is the hot path the Sentinel-vs-C++ measurement
                // was about: one full scan of the buffer per keystroke. See
                // docs/HANDOVER.md phase 49 for the numbers that put it in C++.
                st->note.clear();
                st->haveCurrent = false;
                recompute(st, +1);
                return 0;
            }
            if (id == kFindReplaceField && code == EN_CHANGE) {
                st->note.clear();
                updateCount(st);
                return 0;
            }
            switch (id) {
                case kFindNext: step(st, +1); return 0;
                case kFindPrev: step(st, -1); return 0;
                case kFindReplaceOne: replaceCurrent(st); return 0;
                case kFindReplaceAll: replaceAll(st); return 0;
                case kFindMatchCase:
                case kFindWholeWord:
                    st->note.clear();
                    st->haveCurrent = false;
                    recompute(st, +1);
                    SetFocus(st->eFind);
                    return 0;
                case kFindClose:
                case IDCANCEL:
                    hideFindBar(hwnd);
                    return 0;
                case IDOK:
                    // Enter. IsDialogMessageW turns the keystroke into this, and the
                    // modifier has to be read here because that translation does not carry
                    // it. In the replace field Enter means Replace; anywhere else it means
                    // find, forwards or (with Shift) backwards.
                    if (GetFocus() == st->eRepl)
                        replaceCurrent(st);
                    else
                        step(st, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : +1);
                    return 0;
                default: break;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

HWND createFindBar(HWND parent, HWND editor, HINSTANCE hInst, UINT dpi, HFONT ui) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc;
        wc.hInstance = hInst;
        wc.lpszClassName = kFindBarClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // WM_ERASEBKGND paints the themed band
        if (!RegisterClassExW(&wc)) return nullptr;
        reg = true;
    }

    auto* st = new (std::nothrow) FindState();
    if (!st) return nullptr;
    st->parent = parent;
    st->editor = editor;
    st->dpi = dpi ? dpi : 96;
    st->ui = ui;

    // WS_CLIPCHILDREN so the band's erase never paints over its own controls; no
    // WS_VISIBLE, because the bar is created closed and opened by Ctrl+F.
    HWND bar = CreateWindowExW(WS_EX_CONTROLPARENT, kFindBarClass, L"",
                               WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
                               hInst, st);
    if (!bar) {
        delete st;
        return nullptr;
    }
    st->hwnd = bar;

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) -> HWND {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                 bar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 hInst, nullptr);
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui), TRUE);
        return h;
    };

    st->eFind = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, kFindField);
    st->sCount = mk(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, kFindCount);
    // Real BUTTON controls rather than painted rectangles, and it is not laziness: they
    // are what gives this bar Tab order and a name Narrator can read. The main window's
    // toolbar is owner-drawn and has neither, which is a known cost there and not one
    // worth repeating in a surface built around typing.
    st->bPrev = mk(L"BUTTON", L"Previous", BS_PUSHBUTTON | WS_TABSTOP, kFindPrev);
    st->bNext = mk(L"BUTTON", L"Next", BS_PUSHBUTTON | WS_TABSTOP, kFindNext);
    st->kCase = mk(L"BUTTON", L"Match case", BS_AUTOCHECKBOX | WS_TABSTOP, kFindMatchCase);
    st->kWord = mk(L"BUTTON", L"Whole word", BS_AUTOCHECKBOX | WS_TABSTOP, kFindWholeWord);
    st->bClose = mk(L"BUTTON", L"✕", BS_PUSHBUTTON | WS_TABSTOP, kFindClose);
    st->eRepl = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, kFindReplaceField);
    st->bReplace = mk(L"BUTTON", L"Replace", BS_PUSHBUTTON | WS_TABSTOP, kFindReplaceOne);
    st->bReplaceAll = mk(L"BUTTON", L"Replace All", BS_PUSHBUTTON | WS_TABSTOP, kFindReplaceAll);

    showReplaceRow(st, false);
    findBarApplyTheme(bar);
    logMsg(LogLevel::Debug, L"Find bar created");
    return bar;
}

void showFindBar(HWND bar, bool replaceMode) {
    FindState* st = state(bar);
    if (!st) return;
    const bool wasOpen = st->open;
    const bool heightChanges = !wasOpen || (st->replaceMode != replaceMode);
    st->open = true;
    st->replaceMode = replaceMode;
    showReplaceRow(st, replaceMode);
    st->note.clear();

    // Seed from the selection, the way every editor does — but only when the selection is
    // a plausible SEARCH TERM. A multi-line selection is a block someone is about to
    // replace, not a needle, and dropping a 4,000-line paste into a one-line field would
    // wipe whatever they searched for last.
    const std::wstring sel = d2dEditorSelectedText(st->editor);
    if (!sel.empty() && sel.size() <= 200 && sel.find(L'\n') == std::wstring::npos)
        SetWindowTextW(st->eFind, sel.c_str());

    // The origin is pinned HERE, once, on open — see the field's comment for why it must
    // not be re-read from the caret on every keystroke.
    size_t a = 0, b = 0;
    d2dEditorSelection(st->editor, a, b);
    st->origin = a;
    st->haveCurrent = false;

    if (!wasOpen) ShowWindow(bar, SW_SHOW);
    if (heightChanges)
        SendMessageW(st->parent, WM_COMMAND, MAKEWPARAM(kFindBarLayoutCmd, 0),
                     reinterpret_cast<LPARAM>(bar));
    placeChildren(st);
    SetFocus(st->eFind);
    SendMessageW(st->eFind, EM_SETSEL, 0, -1);
    recompute(st, +1);
}

void hideFindBar(HWND bar) {
    FindState* st = state(bar);
    if (!st || !st->open) return;
    st->open = false;
    ShowWindow(bar, SW_HIDE);
    st->matches.clear();
    st->haveCurrent = false;
    st->note.clear();
    // The parent re-lays-out FIRST, so the editor has already grown into the band before
    // it takes the focus — otherwise the caret is scrolled into view against the old,
    // shorter client rect and the view jumps a line as the layout catches up.
    SendMessageW(st->parent, WM_COMMAND, MAKEWPARAM(kFindBarLayoutCmd, 0),
                 reinterpret_cast<LPARAM>(bar));
    SetFocus(st->editor);
}

bool findBarVisible(HWND bar) {
    FindState* st = state(bar);
    return st && st->open;
}

int findBarHeight(HWND bar) {
    FindState* st = state(bar);
    if (!st || !st->open) return 0;
    return barHeightFor(st);
}

void findBarStep(HWND bar, int dir) {
    FindState* st = state(bar);
    if (!st) return;
    if (!st->open) {
        // F3 before Ctrl+F has ever been pressed: open the bar rather than search in
        // silence. showFindBar seeds and selects the first match itself, so a plain F3 on
        // a selected word does the obvious thing in one keystroke.
        showFindBar(bar, false);
        if (dir < 0) step(st, -1);
        return;
    }
    step(st, dir);
}

void findBarBufferChanged(HWND bar) {
    FindState* st = state(bar);
    if (!st || !st->open) return;
    if (st->inEdit > 0) return;  // our own replace, still in flight — see FindState::inEdit
    // pick == 0: the user typed in the EDITOR, so the count must follow but the caret is
    // theirs and must not be dragged to a match.
    recompute(st, 0);
}

void findBarUpdateDpi(HWND bar, UINT dpi, HFONT ui) {
    FindState* st = state(bar);
    if (!st) return;
    st->dpi = dpi ? dpi : 96;
    st->ui = ui;
    for (HWND h : { st->eFind, st->eRepl, st->sCount, st->bPrev, st->bNext, st->kCase,
                    st->kWord, st->bClose, st->bReplace, st->bReplaceAll })
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui), TRUE);
    placeChildren(st);
    if (st->open)
        SendMessageW(st->parent, WM_COMMAND, MAKEWPARAM(kFindBarLayoutCmd, 0),
                     reinterpret_cast<LPARAM>(bar));
}

void findBarApplyTheme(HWND bar) {
    FindState* st = state(bar);
    if (!st) return;
    const bool dark = currentTheme().dark;
    if (HMODULE ux = GetModuleHandleW(L"uxtheme.dll")) {
        using AllowFn = BOOL(WINAPI*)(HWND, BOOL);
        if (auto allow = reinterpret_cast<AllowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133))))
            allow(bar, dark ? TRUE : FALSE);
    }
    EnumChildWindows(bar, [](HWND child, LPARAM dk) -> BOOL {
        SetWindowTheme(child, dk ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        return TRUE;
    }, dark ? 1 : 0);
    InvalidateRect(bar, nullptr, TRUE);
}

FindBarStatus findBarStatus(HWND bar) {
    FindBarStatus s;
    FindState* st = state(bar);
    if (!st) return s;
    s.count = static_cast<int>(st->matches.size());
    s.current = st->haveCurrent ? static_cast<int>(st->current) : -1;
    s.visible = st->open;
    s.replaceMode = st->replaceMode;
    return s;
}

}  // namespace sentinelide
