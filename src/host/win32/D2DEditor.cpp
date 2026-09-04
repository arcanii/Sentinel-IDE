// SPDX-License-Identifier: GPL-3.0-or-later
// D2DEditor.cpp — the no-wrap Direct2D/DirectWrite code editor (slice 2, phase 46).
//
// PROVENANCE, so a reader can tell what was thought about here and what was inherited.
// Carried over essentially verbatim from G:\SQLTerminal-Win32\src\ui\SqlEditorControl.cpp
// (same author, GPL-3.0 — see THIRD-PARTY-NOTICES.txt): the device-resource lifecycle and
// the D2DERR_RECREATE_TARGET path, the clipboard trio, selectWordAt, the caret blink, IME
// positioning, the WM_CHAR surrogate-pair buffering, mouse selection + autoscroll, and the
// WM_NCCREATE/WM_NCDESTROY state lifecycle. Those are solved problems and reinventing them
// would only add bugs.
//
// WRITTEN FRESH: everything to do with layout, geometry and scrolling. The reference is a
// word-WRAPPING query box built on ONE whole-document IDWriteTextLayout. A code editor is
// the opposite case — it must not wrap, and it can be tens of thousands of lines, where a
// document-wide layout is O(document) work on every keystroke. So instead:
//
//   * DWRITE_WORD_WRAPPING_NO_WRAP on the text format;
//   * a lineStarts index rebuilt from EditorModel::text() whenever the text changes;
//   * ONE IDWriteTextLayout PER LINE, created lazily, cached, and trimmed back to the
//     visible window after every paint (see layoutForLine / trimCache);
//   * scrollX AND scrollY with both scrollbars, WM_HSCROLL/WM_VSCROLL/WM_MOUSEWHEEL;
//   * paint clipped to the visible line range.
//
// The pleasant consequence of no-wrap: a VISUAL line is a LOGICAL line, so vertical
// navigation and hit-testing are per-line arithmetic (index +/- 1, y / lineH) instead of
// DWrite hit-testing across a wrapped document. That half is simpler than the reference,
// not harder — and it is why WM_SIZE here does not invalidate any layout: changing the
// width cannot re-wrap anything.
//
// ADDED IN SLICE 3: the RichEdit message dialect and the parent-notification funnel, so
// MainWindow can drive this control with (almost) no feature branches. See "the RichEdit
// dialect" section in D2DEditorProc below, and the funnel just above drawContent.
//
// ADDED IN SLICE 4: syntax colouring and the error-line tints, both as PAINTED DECORATION.
// Two rules govern how, and both are load-bearing rather than stylistic:
//
//   1. NEVER IDWriteTextLayout::SetDrawingEffect. A drawing effect is a BRUSH, i.e. a
//      device resource, and the per-line layout cache is shared between the window's
//      ID2D1HwndRenderTarget and the offscreen WIC target AND survives device loss
//      precisely because it holds none. drawContent therefore colours by DRAWING: the same
//      one layout is drawn once per coloured run inside an axis-aligned clip covering that
//      run's x-range. Same layout every time means identical shaping and identical advance
//      widths, so painting and hit-testing cannot drift apart.
//   2. NEVER colour by moving the selection. That is RichEdit's EM_EXSETSEL storm, which
//      reaches EditorModel::setSelection, clears typingRun_ and would make every keystroke
//      its own undo step. Nothing in the colouring path touches the model at all.
//
// The rules themselves are NOT here: they live in src/editor/SyntaxLexer.{h,cpp}, which the
// RichEdit path calls too, so the two editors cannot disagree about a file.
//
// ADDED IN SLICE 7: OLE DRAG AND DROP — the last behavioural difference against RichEdit,
// and the precondition for deleting the RichEdit path. Drag a selection to move it,
// Ctrl-drag to copy, accept CF_UNICODETEXT from other applications, and — the part that is
// a REGRESSION RISK rather than a feature — keep file drops working. Registering a drop
// target on this child takes file drops away from the main window's WM_DROPFILES, so
// CF_HDROP is checked first and handed back to the host's own guarded opening path. The
// whole thing is in one section, "OLE drag and drop", with its four traps written out
// there.
#include "host/win32/D2DEditor.h"

#include <windowsx.h>
#include <imm.h>
#include <wincodec.h>  // WIC — the offscreen render path only (d2dEditorRenderToPng)

// AFTER D2DEditor.h (which includes <windows.h> first) — NEVER before it, and this is not
// stylistic. richedit.h guards EM_POSFROMCHAR and EM_SCROLLCARET with #ifndef, so whichever
// header is seen first wins: windows.h gives 0x00D6 / 0x00B7, richedit.h alone would give
// WM_USER+38 / WM_USER+49. MainWindow.cpp includes windows.h at :32 and richedit.h at :35,
// so it SENDS 0x00D6. Get the order wrong here and this file compiles `case EM_POSFROMCHAR:`
// as a different number, the message falls through to DefWindowProcW returning 0, the POINTL
// is left {0,0} — and every line number in the gutter draws stacked at y=0 while the loop's
// only exit test (`if (pt.y > edH) break`, MainWindow.cpp:410) never fires, so it walks the
// whole document on every repaint. Zero warnings, zero errors, one very confused reader.
#include <richedit.h>

// OLE drag-and-drop (slice 7): IDataObject / IDropSource / IDropTarget, RegisterDragDrop,
// DoDragDrop, ReleaseStgMedium. Included explicitly rather than leaned on windows.h for —
// this file's drop path is not optional plumbing and the dependency should be visible.
#include <ole2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <new>
#include <string>
#include <vector>

#include "core/Logger.h"
#include "editor/EditorModel.h"
#include "editor/SyntaxLexer.h"
#include "host/win32/D2DSupport.h"
#include "host/win32/Theme.h"

namespace sentinelide {

namespace {

// Text inset (96-dpi design), inherited from the reference (NSTextView's 4x6).
constexpr int kInsetX = 4;
constexpr int kInsetY = 6;
constexpr UINT_PTR kBlinkTimer = 1;
constexpr UINT_PTR kAutoScrollTimer = 2;
// Retry-after-device-loss. A bare InvalidateRect from inside WM_PAINT re-dirties the window
// the instant it is validated, so if the device is permanently unobtainable (D2D blocked,
// no GPU) the result is a WM_PAINT loop pinning the UI thread at 100% -- measured, not
// theorised. A one-shot timer gives the retry a floor instead.
constexpr UINT_PTR kDeviceRetryTimer = 3;
constexpr UINT kDeviceRetryMs = 250;
// How long a device failure has to persist before paint() stops retrying in silence and
// says so on the client area. Long enough that a GPU reset or an RDP session change (which
// recover within a frame or two) never flashes a notice at the user; short enough that a
// real one is explained before anybody starts typing into it. See paint's else branch —
// since phase 46 slice 8 removed the RichEdit editor there is no fallback control, so a
// blank pane with no explanation is the outcome that has to be impossible.
constexpr ULONGLONG kDeviceFailQuietMs = 1000;

// The editor font. Matches Settings::editorFont's default and the 11pt the RichEdit
// control is formatted with, so the swap in slice 6 is not also a font change.
// d2dEditorSetFont is the seam slice 3 uses to feed the real setting in.
constexpr const wchar_t* kDefaultFace = L"Cascadia Code";
constexpr float kDefaultPointSize = 11.0f;

// Tab width in columns (monospace, so column == advance of '0').
constexpr int kTabColumns = 4;

// ---- drag and drop (slice 7) ------------------------------------------------
//
// The window property under which the control publishes its IDropTarget. It exists for ONE
// caller — tests/d2d_dialect_test.cpp — because RegisterDragDrop has no public getter and
// the alternative was a second, test-only drop path, which is exactly the shape of test
// that passes while the shipping path is broken. Through this the test drives the REAL
// DragEnter/DragOver/Drop with its own IDataObject: format negotiation, the
// inside-the-selection rejection, the key-state-to-effect mapping and the CF_HDROP
// forwarding are all the code that ships. (ole32 stores its own copy under
// "OleDropTargetInterface"; that name is an undocumented implementation detail and is NOT
// what this reads. This property is ours, set and removed by this file.)
constexpr const wchar_t* kDropTargetProp = L"SentinelD2DDropTarget";

// The private clipboard format that says "this drag came out of a Sentinel Direct2D
// editor", carrying the source HWND as a UINT64 payload.
//
// WHY: a drop has to know whether it is a MOVE WITHIN THIS CONTROL — which deletes the
// source selection — or an insert from somewhere else, which must never delete anything.
// Deciding that from "is DoDragDrop on our stack?" would be wrong twice: it cannot be
// tested without a mouse, and it would call a drag from a SECOND editor in this process
// local to the first. Identity travels on the data object, where it belongs.
UINT sentinelDragFormat() {
    // Function-local static: registered once per process, on first use, and the clipboard
    // format registry is per-session so the value is stable for every window here.
    static const UINT cf = RegisterClipboardFormatW(L"SentinelD2DEditorDrag");
    return cf;
}

// How many lines either side of the visible window keep their layout after a paint.
// This is the whole memory bound: live IDWriteTextLayouts <= visible + 2*kCacheMargin,
// regardless of how big the document is or how far it has been scrolled.
constexpr size_t kCacheMargin = 64;

// Per-control state, hung off GWLP_USERDATA (lifecycle from the reference).
struct EditorState {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    editor::EditorModel model;

    // Device-dependent (recreated on resize / device loss).
    //
    // rt is the BASE type deliberately. Everything this control draws through goes via
    // ID2D1RenderTarget, so drawContent() takes the target as a parameter and the offscreen
    // WIC path (d2dEditorRenderToPng) can substitute a bitmap target for the length of one
    // draw. hwndRt is the SAME OBJECT, kept typed only for Resize() — the one method used
    // here that the base interface does not have (CreateHwndRenderTarget, which produces it,
    // being the other).
    // TRAP: one COM object, ONE reference. Release through rt only and null both
    // (discardDeviceResources does exactly that); releasing through each would be a
    // double-release, i.e. a use-after-free on the next paint.
    ID2D1RenderTarget* rt = nullptr;
    ID2D1HwndRenderTarget* hwndRt = nullptr;
    // EVERY device-bound brush lives in ONE struct, and that is load-bearing rather than
    // tidy. Three places must agree about the whole set — createBrushes, releaseBrushes, and
    // d2dEditorRenderToPng's park-and-restore — and when the members were loose fields, slice
    // 4 added five brushes to the first two and MISSED THE THIRD. The result was silent: an
    // offscreen render destroyed the window's five new brushes while the window still owned
    // them, brushForClass then returned null, drawRun fell back to brText, and the live
    // editor painted MONOCHROME for the rest of its life with no crash and no leak. Grouped
    // like this, park/restore is a struct copy that cannot omit a member, so adding a brush
    // is one line in one place. Do not flatten this back out.
    struct Brushes {
        ID2D1SolidColorBrush* text = nullptr;
        ID2D1SolidColorBrush* selection = nullptr;
        ID2D1SolidColorBrush* caret = nullptr;
        // Slice 4. One brush per SpanClass plus the error-line tint. They are DEVICE
        // resources and live and die with the target, exactly like the three above — which
        // is the whole reason the colours are applied by DRAWING rather than by hanging them
        // off the (device-independent, cross-target, device-loss-surviving) layout cache.
        ID2D1SolidColorBrush* comment = nullptr;
        ID2D1SolidColorBrush* str = nullptr;
        ID2D1SolidColorBrush* number = nullptr;
        ID2D1SolidColorBrush* keyword = nullptr;
        ID2D1SolidColorBrush* errorTint = nullptr;
    };
    Brushes br;

    // Device-independent text resources.
    IDWriteTextFormat* format = nullptr;
    std::wstring face = kDefaultFace;
    float pointSize = kDefaultPointSize;
    float lineH = 0.0f;   // one line's height in pixels (RT dpi is 96, so DIP == pixel)
    float spaceW = 0.0f;  // advance of '0'; the horizontal "line" scroll unit

    // ---- the line index -----------------------------------------------------
    // lineStarts[i] is the offset of line i's first code unit. Always non-empty:
    // an empty document is one empty line. Rebuilt wholesale from model.text() when
    // linesDirty — deliberately NOT patched incrementally in this slice, because a
    // wrong line index is a silently-corrupt editor and the scan is a memchr over a
    // buffer we already own.
    std::vector<size_t> lineStarts;
    bool linesDirty = true;

    // ---- the per-line lexer start-state cache (slice 4) ---------------------
    // lineLexState[i] is the SyntaxLexer state at the first character of line i, so a
    // visible line can be coloured on its own without lexing the document in front of it.
    // It is a PREFIX cache: entries [0, lexStateKnown) are valid, lexStateKnown >= 1, and
    // lineLexState[0] is always the default (start of document).
    //
    // WHY A PREFIX AND NOT A FULL REBUILD. Painting needs the state of the FIRST VISIBLE
    // line, so a cache thrown away on every edit would re-lex everything above the caret on
    // every keystroke — 19,000 lines if you are typing at line 19,000, which is the one
    // cost this cache exists to remove. Instead rebuildLineIndex only ever *lowers* the
    // watermark, to the line the edit actually touched, so an edit below the watermark
    // costs no lexing at all and an edit above it costs the visible window.
    std::vector<editor::LexState> lineLexState;
    size_t lexStateKnown = 0;

    // The document as it was when the line index was last rebuilt, kept ONLY to find the
    // first character an edit changed (see rebuildLineIndex). One memcmp + one copy of the
    // buffer per edit, which is the same order as the '\n' scan in that same function and
    // the two full-document passes MainWindow::onEditChanged already makes per keystroke —
    // and far cheaper than the re-lex it buys away. Deriving the edit position from the
    // caret instead would be free, but wrong for paste-over-a-selection and undo, and a
    // stale colouring cache is a bug you only see on someone else's file.
    std::wstring prevText;

    // Scratch for drawContent's per-line spans. A member, not a local, so painting 40 lines
    // reuses one allocation instead of making 40.
    std::vector<editor::Span> spanScratch;

    // Lines (0-based) to tint as errors, sorted and unique. The host sets this from
    // g.problems after a build; it is DECORATION — no selection is moved and no character
    // format exists, which is exactly what keeps undo granularity intact around a build.
    std::vector<int> errorLines;

    // ---- the per-line layout cache -----------------------------------------
    // lineLayouts is parallel to lineStarts and is almost entirely nullptr: 8 bytes a
    // line of index (160 KB for a 20k-line file), but only the handful of LAYOUTS that
    // are on screen. `cached` lists exactly the populated indices so trimCache is
    // O(live), never O(document).
    std::vector<IDWriteTextLayout*> lineLayouts;
    std::vector<size_t> cached;

    // Horizontal extent: the widest line LAID OUT SO FAR, i.e. a high-water mark, not
    // the true maximum — nothing has measured the lines that were never on screen. Every
    // real editor does this (Scintilla calls it the scroll-width high-water mark); the
    // scrollbar therefore grows as you explore the file and is only reset on setText,
    // font change or DPI change, where every cached width is stale anyway. Note what that
    // excludes: EDITS never shrink it, so deleting the longest line leaves the horizontal
    // range over-wide (scrollable into blank space) until the next load. Scintilla behaves
    // the same way. Recomputing it would mean measuring every line, which is the one thing
    // the per-line cache exists to avoid.
    float maxLineW = 0.0f;

    // View state.
    int scrollX = 0;
    int scrollY = 0;
    float desiredX = -1.0f;  // sticky column for up/down, line-local px; <0 = recompute
    bool hasFocus = false;
    bool selecting = false;
    bool caretOn = true;
    wchar_t pendingHigh = 0;  // buffered high surrogate from WM_CHAR
    UINT_PTR blinkTimer = 0;
    UINT_PTR autoTimer = 0;

    // ---- drag and drop (slice 7) -------------------------------------------
    // The registered drop target. Null when RegisterDragDrop failed — which is not an
    // error worth failing creation over: the thread may have no OLE apartment (the
    // standalone demo and the render test both CoInitializeEx rather than OleInitialize),
    // and an editor without drag-and-drop is exactly the editor slices 2-6 shipped. Every
    // drag path below is gated on this being non-null, so "no OLE" degrades to the old
    // behaviour rather than to a half-registered control.
    IDropTarget* dropTarget = nullptr;

    // A left-press landed INSIDE the selection, so it might be the start of a drag. The
    // caret is deliberately NOT moved yet — that is decided at the threshold (drag) or at
    // the button-up (an ordinary click). dragArmPt is the press point, in client pixels.
    bool dragArmed = false;
    POINT dragArmPt{ 0, 0 };

    // DoDragDrop is on the stack for THIS control. A MODAL LOOP: it pumps messages, so
    // paints, timers and host-posted work can all run while it is set.
    bool dragActive = false;
    // ...and our own drop target already performed the move, so the source half must NOT
    // delete the selection a second time when DoDragDrop returns DROPEFFECT_MOVE.
    bool dragLocalDrop = false;

    // The drop caret: where the payload would land, tracked during a drag. It is NOT the
    // model caret — a local drag must keep its source selection on screen, and moving the
    // real caret would collapse it. Painted unblinking and even without focus (a drag from
    // another application does not give us the focus until the drop).
    bool dropCaret = false;
    size_t dropOffset = 0;

    // The SYSTEM caret (slice 5) — created while focused, never shown. See
    // syncSystemCaret. sysCaretPt/sysCaretH are the values last pushed to USER32, so a
    // sync that changes nothing costs no call and raises no accessibility event.
    bool sysCaret = false;
    POINT sysCaretPt{ -1, -1 };
    int sysCaretH = 0;

    // ---- the RichEdit dialect (slice 3) -------------------------------------
    // EM_SETEVENTMASK's mask. THE DEFAULT IS EVERY BIT SET, NOT ZERO, and that is the
    // whole point of writing it out: a mask that starts empty and then somehow misses
    // that one call (a reorder of createControls, say) is SILENT — no EN_CHANGE, g.dirty
    // never set, buffers discarded with no prompt. Failing towards "too many
    // notifications" costs a few repaints; failing the other way costs a user's work.
    DWORD eventMask = ENM_CHANGE | ENM_SELCHANGE | ENM_SCROLL;

    // WM_SETREDRAW(FALSE) nesting depth. What the COUNTER buys over a bool is correct
    // NESTING (on the RichEdit path clearErrorMarks/markErrorLines ran inside highlight()'s
    // window, so the inner TRUE must not un-mute the outer one) and immunity to a stray
    // extra TRUE (it floors at 0 instead of going negative). It buys NOTHING
    // against an unbalanced FALSE — quite the reverse: an unbalanced FALSE parks this at 1
    // forever and EN_SELCHANGE is muted for the life of the window, where a bool would be
    // cleared by the very next TRUE. The host's four windows are all straight-line
    // FALSE…TRUE pairs with no early return between them, which is why that is acceptable;
    // it is a precondition on the caller, not a property of the counter. See the
    // WM_SETREDRAW case for what the flag is actually used for (it is not redraw
    // suppression). SINCE SLICE 8 THE HOST SENDS NO WM_SETREDRAW AT ALL — every sender was
    // in the deleted RichEdit colouring path — so this sits at 0 for the life of the window.
    // Kept because it is part of the dialect this class answers (and d2d_dialect pins it),
    // not because anything in the IDE still exercises it.
    int redrawOff = 0;

    // >0 while the host is re-entering this control from inside one of our own
    // notifications. Suppresses the *view* notifications only — never a mutation, and
    // never EN_CHANGE, which must be impossible to lose.
    int notifyDepth = 0;

    // Last values actually reported to the parent, for the diff in flushViewNotifications.
    int notifiedScrollX = 0;
    int notifiedScrollY = 0;
    editor::Selection notifiedSel{};

    // EM_SETBKGNDCOLOR. Stored rather than ignored so this and drawContent's clear cannot
    // drift apart; unset means "whatever the current Theme says".
    COLORREF bkColor = 0;
    bool haveBkColor = false;

    // Device-failure reporting (slice 8). deviceFailSince is the tick of the FIRST paint in
    // the current run of failures (0 = the device is fine); deviceFailReported stops the log
    // line repeating four times a second. Both reset the moment a paint succeeds.
    ULONGLONG deviceFailSince = 0;
    bool deviceFailReported = false;
};

EditorState* state(HWND h) {
    return reinterpret_cast<EditorState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

int dpx(EditorState* st, int v) { return MulDiv(v, static_cast<int>(st->dpi), 96); }

// Must agree with EditorModel.cpp's word-class predicate (Sentinel's Unicode-aware
// `iswalnum(c) || c == '_'`, NOT SQLTerminal's ASCII-only one). TRAP: that predicate lives
// in an anonymous namespace in EditorModel.cpp, so this is a second copy rather than a
// shared call. If one is ever changed, change both — double-click selection and
// Ctrl+Left/Right would otherwise disagree about where a word ends.
bool isWordChar(wchar_t c) { return iswalnum(c) != 0 || c == L'_'; }

// ---- text format ------------------------------------------------------------

void measureMetrics(EditorState* st) {
    st->lineH = 0;
    st->spaceW = 0;
    IDWriteFactory* dw = dwriteFactory();
    if (!dw || !st->format) return;
    IDWriteTextLayout* tmp = nullptr;
    if (SUCCEEDED(dw->CreateTextLayout(L"Ag", 2, st->format, 1.0e5f, 1.0e5f, &tmp)) && tmp) {
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(tmp->GetMetrics(&tm))) st->lineH = tm.height;
        tmp->Release();
        tmp = nullptr;
    }
    if (SUCCEEDED(dw->CreateTextLayout(L"0", 1, st->format, 1.0e5f, 1.0e5f, &tmp)) && tmp) {
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(tmp->GetMetrics(&tm))) st->spaceW = tm.widthIncludingTrailingWhitespace;
        tmp->Release();
    }
    if (st->lineH <= 0) st->lineH = static_cast<float>(dpx(st, 16));
    if (st->spaceW <= 0) st->spaceW = static_cast<float>(dpx(st, 8));
    // A code editor's tabs are column stops, not DWrite's default 4-em guess.
    st->format->SetIncrementalTabStop(st->spaceW * kTabColumns);
}

// Idempotent, and the ONLY producer of st->lineH / st->spaceW (via measureMetrics). Every
// function that divides by or multiplies with those must therefore call this first, or it
// silently computes against lineH == 0. Cheap after the first call — one pointer test —
// so the rule is "call it, don't reason about who called it for you". See EM_POSFROMCHAR,
// visibleRange and contentHeight, which are reachable from the PARENT's paint before this
// control has ever laid a line out.
void ensureFormat(EditorState* st) {
    if (st->format) return;
    IDWriteFactory* dw = dwriteFactory();
    if (!dw) return;
    // The render target's DPI is pinned at 96 so 1 DIP == 1 physical pixel; the em-size
    // therefore carries the DPI scaling itself (11pt at 96dpi == 14.67px).
    const float emSize = st->pointSize * static_cast<float>(st->dpi) / 72.0f;
    if (FAILED(dw->CreateTextFormat(st->face.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, emSize,
                                    L"", &st->format)) ||
        !st->format) {
        st->format = nullptr;
        return;
    }
    // THE line that separates this control from the reference: code does not wrap.
    st->format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    measureMetrics(st);
}

// ---- brushes ----------------------------------------------------------------

void releaseBrushes(EditorState* st) {
    SafeRelease(st->br.text);
    SafeRelease(st->br.selection);
    SafeRelease(st->br.caret);
    SafeRelease(st->br.comment);
    SafeRelease(st->br.str);
    SafeRelease(st->br.number);
    SafeRelease(st->br.keyword);
    SafeRelease(st->br.errorTint);
}

// Returns false if any brush failed, so paint never runs with a partially-null set.
bool createBrushes(EditorState* st) {
    releaseBrushes(st);
    if (!st->rt) return false;
    const Theme& th = currentTheme();
    bool ok = true;
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.textPrimary), &st->br.text));
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.selectionBg), &st->br.selection));
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.textPrimary), &st->br.caret));
    // Slice 4. The syntax palette is the SAME Theme the RichEdit path colours with, read
    // here rather than passed in by the host, so a light/dark flip repaints correctly with
    // no host call — d2dEditorApplyTheme and WM_THEMECHANGED both land in this function.
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.synComment), &st->br.comment));
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.synString), &st->br.str));
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.synNumber), &st->br.number));
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(colorToD2D(th.synKeyword), &st->br.keyword));
    // The error-line band. blendColor lives in Theme.h so this and markErrorLines'
    // RichEdit branch are literally the same arithmetic on the same two tokens — the tint
    // must not change when the editor does, since slice 6 flips which one you see.
    ok &= SUCCEEDED(st->rt->CreateSolidColorBrush(
        colorToD2D(blendColor(th.windowBg, th.diagError, 24)), &st->br.errorTint));
    return ok;
}

// The brush for a span class. Default is the ordinary text brush, which is why
// SpanClass::Default is never emitted by the lexer and is synthesised by the painter for
// the gaps between spans.
ID2D1SolidColorBrush* brushForClass(EditorState* st, editor::SpanClass cls) {
    switch (cls) {
        case editor::SpanClass::Comment: return st->br.comment;
        case editor::SpanClass::String:  return st->br.str;
        case editor::SpanClass::Number:  return st->br.number;
        case editor::SpanClass::Keyword: return st->br.keyword;
        case editor::SpanClass::Default: break;
    }
    return st->br.text;
}

// ---- device resources (carried over from the reference) ---------------------

void discardDeviceResources(EditorState* st) {
    releaseBrushes(st);
    SafeRelease(st->rt);   // the single reference — hwndRt only ALIASES it
    st->hwndRt = nullptr;  // ...so it is nulled, never released
}

// Idempotent — called at the top of every paint, so device-loss recovery and cold start
// are literally the same path.
bool ensureDeviceResources(EditorState* st) {
    if (!d2dFactory() || !dwriteFactory()) return false;
    ensureFormat(st);
    if (st->rt) return true;

    RECT rc;
    GetClientRect(st->hwnd, &rc);
    const D2D1_SIZE_U size =
        D2D1::SizeU(static_cast<UINT32>((std::max<LONG>)(1, rc.right - rc.left)),
                    static_cast<UINT32>((std::max<LONG>)(1, rc.bottom - rc.top)));
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(st->hwnd, size);
    if (FAILED(d2dFactory()->CreateHwndRenderTarget(props, hp, &st->hwndRt)) || !st->hwndRt) {
        st->hwndRt = nullptr;
        return false;
    }
    st->rt = st->hwndRt;  // one object, one reference; see EditorState::rt
    st->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    if (!createBrushes(st)) {
        discardDeviceResources(st);
        return false;
    }
    // NOTE: unlike the reference, the layouts are NOT invalidated here. They carry no
    // drawing effects — slice 4's colouring is applied by DRAWING each run with a
    // different brush, never by SetDrawingEffect — so losing the device does not
    // invalidate a single one of them.
    return true;
}

// ---- the line index ---------------------------------------------------------

void dropLayoutCache(EditorState* st) {
    for (size_t i : st->cached) {
        if (i < st->lineLayouts.size()) SafeRelease(st->lineLayouts[i]);
    }
    st->cached.clear();
}

void rebuildLineIndex(EditorState* st) {
    dropLayoutCache(st);
    st->lineStarts.clear();
    st->lineStarts.push_back(0);
    const std::wstring& t = st->model.text();
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == L'\n') st->lineStarts.push_back(i + 1);
    }
    st->lineLayouts.assign(st->lineStarts.size(), nullptr);
    st->linesDirty = false;

    // ---- and the lexer start-state cache, which is invalidated HERE and nowhere else ---
    // The state at the start of line L depends only on text[0, lineStarts[L]), so every
    // cached entry up to and including the line that contains the first CHANGED character
    // is still exactly right. Find that character by comparing against the buffer as it was
    // last time through here; everything from there down is dropped and re-lexed lazily.
    //
    // This is the whole reason typing at line 19,000 of a 20,000-line file does not re-lex
    // 19,000 lines: the watermark stays at 19,000, and the visible window is above it.
    // Doing it in this one function is deliberate — it is the single place the text is
    // known to have changed, so no mutation path (paste, undo, WM_SETTEXT, a future one)
    // can forget to invalidate. TRAP: the obvious cheap alternative, "invalidate from the
    // caret's line", is WRONG for a paste over a multi-line selection (the caret ends below
    // the first changed line) and for undo (which can change anything).
    const size_t lim = (std::min)(st->prevText.size(), t.size());
    size_t firstDiff = 0;
    while (firstDiff < lim && st->prevText[firstDiff] == t[firstDiff]) ++firstDiff;
    // Equal prefixes and equal lengths means nothing changed at all (an "edit" that
    // replaced text with itself); anything else changed at firstDiff.
    const bool identical = (firstDiff == lim && st->prevText.size() == t.size());
    st->prevText = t;

    st->lineLexState.resize(st->lineStarts.size());
    if (st->lineLexState.empty()) st->lineLexState.push_back(editor::LexState{});
    st->lineLexState[0] = editor::LexState{};
    if (st->lexStateKnown < 1) st->lexStateKnown = 1;
    if (!identical) {
        // upper_bound - 1, the same arithmetic lineIndexForOffset uses; written out
        // because lineIndexForOffset is defined below this function.
        const auto it =
            std::upper_bound(st->lineStarts.begin(), st->lineStarts.end(), firstDiff);
        const size_t changedLine = static_cast<size_t>(it - st->lineStarts.begin()) - 1;
        st->lexStateKnown = (std::min)(st->lexStateKnown, changedLine + 1);
    }
    st->lexStateKnown = (std::min)(st->lexStateKnown, st->lineLexState.size());
}

void ensureLineIndex(EditorState* st) {
    if (st->linesDirty || st->lineStarts.empty()) rebuildLineIndex(st);
}

size_t lineCount(EditorState* st) { return st->lineStarts.size(); }

// End of line i, EXCLUDING its '\n' (lineStarts[i+1] points one past that '\n').
size_t lineEndOffset(EditorState* st, size_t i) {
    return (i + 1 < st->lineStarts.size()) ? st->lineStarts[i + 1] - 1 : st->model.length();
}

size_t lineIndexForOffset(EditorState* st, size_t off) {
    const auto it = std::upper_bound(st->lineStarts.begin(), st->lineStarts.end(), off);
    return static_cast<size_t>(it - st->lineStarts.begin()) - 1;  // lineStarts[0] == 0
}

// The lexer state at the first character of line i, extending the prefix cache as far as
// it has to and no further. THE COST MODEL, in one place:
//   * a cache HIT (the usual case while typing, because rebuildLineIndex only lowers the
//     watermark to the edited line) is a vector index — O(1);
//   * a MISS costs one pass of advanceState over the lines between the watermark and i,
//     which happens when you scroll into territory this document has not been coloured in
//     yet, or type ABOVE the window. Typing on line 1 of a 20,000-line file drops the
//     watermark to 1 and the next paint re-lexes only the ~40 lines it is about to draw.
// Nothing here allocates: advanceState is the span-free half of the same lexer.
const editor::LexState& lineStartState(EditorState* st, size_t i) {
    ensureLineIndex(st);
    if (st->lexStateKnown < 1) {  // ensureLineIndex always leaves this >= 1; belt and braces
        st->lineLexState.resize((std::max<size_t>)(1, st->lineStarts.size()));
        st->lineLexState[0] = editor::LexState{};
        st->lexStateKnown = 1;
    }
    const std::wstring& t = st->model.text();
    while (st->lexStateKnown <= i && st->lexStateKnown < st->lineStarts.size()) {
        const size_t prev = st->lexStateKnown - 1;
        editor::LexState s = st->lineLexState[prev];
        const size_t a = st->lineStarts[prev];
        const size_t b = lineEndOffset(st, prev);  // excludes the '\n', as the lexer expects
        editor::advanceState(t.c_str() + a, b - a, s);
        st->lineLexState[st->lexStateKnown] = s;
        ++st->lexStateKnown;
    }
    const size_t k = (i < st->lineLexState.size()) ? i : 0;
    return st->lineLexState[k];
}

// ---- the per-line layout cache ---------------------------------------------
// THE bound on this control's cost. Nothing outside this function creates a layout, and
// its only callers are the visible-range paint loop and the single-line geometry helpers
// (caret / hit test / sticky column), which name ONE line each. So painting 40 lines of a
// 20,000-line file creates 40 layouts, not 20,000, and trimCache below keeps it that way
// across arbitrary scrolling.
IDWriteTextLayout* layoutForLine(EditorState* st, size_t i) {
    ensureLineIndex(st);
    if (i >= st->lineStarts.size()) return nullptr;
    if (st->lineLayouts[i]) return st->lineLayouts[i];
    ensureFormat(st);
    IDWriteFactory* dw = dwriteFactory();
    if (!dw || !st->format) return nullptr;

    const std::wstring& t = st->model.text();
    const size_t a = st->lineStarts[i];
    const size_t b = lineEndOffset(st, i);
    IDWriteTextLayout* layout = nullptr;
    // t.c_str() + a is always dereferenceable (it lands on '\n' or the NUL for a
    // zero-length line), so an empty line is a legal zero-length layout, not a null deref.
    if (FAILED(dw->CreateTextLayout(t.c_str() + a, static_cast<UINT32>(b - a), st->format, 1.0e6f,
                                    1.0e6f, &layout)) ||
        !layout) {
        return nullptr;
    }
    st->lineLayouts[i] = layout;
    st->cached.push_back(i);

    DWRITE_TEXT_METRICS tm{};
    if (SUCCEEDED(layout->GetMetrics(&tm)) && tm.widthIncludingTrailingWhitespace > st->maxLineW)
        st->maxLineW = tm.widthIncludingTrailingWhitespace;
    return layout;
}

// Release every cached layout outside [keepLo, keepHi). O(live layouts) — `cached` holds
// only the populated indices, so this never walks the document.
void trimCache(EditorState* st, size_t keepLo, size_t keepHi) {
    size_t w = 0;
    for (size_t k = 0; k < st->cached.size(); ++k) {
        const size_t i = st->cached[k];
        if (i < st->lineLayouts.size() && i >= keepLo && i < keepHi) {
            st->cached[w++] = i;
        } else if (i < st->lineLayouts.size()) {
            SafeRelease(st->lineLayouts[i]);
        }
    }
    st->cached.resize(w);
}

// ---- geometry (per-line arithmetic; no document-wide hit testing) ------------

// x of `off` within line `i`, in line-local pixels (no inset, no scroll).
float xInLine(EditorState* st, size_t i, size_t off) {
    IDWriteTextLayout* layout = layoutForLine(st, i);
    if (!layout) return 0.0f;
    const size_t a = st->lineStarts[i];
    const size_t b = lineEndOffset(st, i);
    const size_t clamped = (off < a) ? a : ((off > b) ? b : off);
    DWRITE_HIT_TEST_METRICS m{};
    float px = 0, py = 0;
    if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(clamped - a), FALSE, &px, &py, &m)))
        return 0.0f;
    return px;
}

// Inverse of xInLine: the offset nearest line-local x. Cannot escape the line, because
// the layout only ever contains that line's text.
size_t offsetFromXInLine(EditorState* st, size_t i, float x) {
    IDWriteTextLayout* layout = layoutForLine(st, i);
    if (i >= st->lineStarts.size()) return st->model.docEnd();
    if (!layout) return st->lineStarts[i];
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    if (FAILED(layout->HitTestPoint(x, st->lineH * 0.5f, &trailing, &inside, &m)))
        return st->lineStarts[i];
    return st->lineStarts[i] + static_cast<size_t>(m.textPosition) +
           (trailing ? static_cast<size_t>(m.length) : 0);
}

int clientW(EditorState* st) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    return rc.right - rc.left;
}
int clientH(EditorState* st) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    return rc.bottom - rc.top;
}

int contentHeight(EditorState* st) {
    ensureLineIndex(st);
    ensureFormat(st);  // lineH, or the whole document measures zero pixels tall
    return static_cast<int>(static_cast<float>(lineCount(st)) * st->lineH) + dpx(st, kInsetY) * 2;
}
int contentWidth(EditorState* st) {
    return static_cast<int>(st->maxLineW) + dpx(st, kInsetX) * 2;
}

void clampScroll(EditorState* st) {
    const int maxY = (std::max)(0, contentHeight(st) - clientH(st));
    const int maxX = (std::max)(0, contentWidth(st) - clientW(st));
    st->scrollY = (std::min)((std::max)(0, st->scrollY), maxY);
    st->scrollX = (std::min)((std::max)(0, st->scrollX), maxX);
}

// The half-open visible line range [first, last]. Everything downstream — paint, cache
// trimming — is driven off this, which is what keeps the cost proportional to the window
// rather than to the file.
void visibleRange(EditorState* st, size_t& first, size_t& last) {
    ensureLineIndex(st);
    ensureFormat(st);  // lineH is the divisor below; EM_GETFIRSTVISIBLELINE reaches here
    const size_t n = lineCount(st);
    const float lh = (std::max)(1.0f, st->lineH);
    const float top = static_cast<float>(st->scrollY - dpx(st, kInsetY));
    const float bot = top + static_cast<float>(clientH(st));
    long long f = static_cast<long long>(std::floor(top / lh));
    long long l = static_cast<long long>(std::floor(bot / lh));
    if (f < 0) f = 0;
    if (l < 0) l = 0;
    if (f >= static_cast<long long>(n)) f = static_cast<long long>(n) - 1;
    if (l >= static_cast<long long>(n)) l = static_cast<long long>(n) - 1;
    first = static_cast<size_t>(f);
    last = static_cast<size_t>(l);
}

void ensureCaretVisible(EditorState* st) {
    ensureLineIndex(st);
    const size_t line = lineIndexForOffset(st, st->model.caret());
    const int insetX = dpx(st, kInsetX);
    const int insetY = dpx(st, kInsetY);
    const int viewH = clientH(st);
    const int viewW = clientW(st);

    // The inset is padding INSIDE the view, not content, so the scroll target must not
    // carry it: scrolling to (insetY + line*lineH) to reveal line 0 lands on insetY rather
    // than 0, which left Ctrl+Home eating the top padding with the thumb off the top, and
    // arrowing to the last line left it flush against the frame. Keep the inset out of the
    // target and add it back only where it is genuinely wanted -- as trailing-edge margin.
    const int top = static_cast<int>(static_cast<float>(line) * st->lineH);
    const int bot = top + static_cast<int>(st->lineH) + 2 * insetY;
    if (top < st->scrollY)
        st->scrollY = top;
    else if (bot > st->scrollY + viewH)
        st->scrollY = bot - viewH;

    const int cx = static_cast<int>(xInLine(st, line, st->model.caret()));
    if (cx < st->scrollX)
        st->scrollX = cx;
    else if (cx + 2 * insetX > st->scrollX + viewW)
        st->scrollX = cx + 2 * insetX - viewW;

    clampScroll(st);
}

// SIF_DISABLENOSCROLL matters more than it looks: the control is created WS_HSCROLL |
// WS_VSCROLL, and letting Windows HIDE a bar would change the client size — from inside
// WM_PAINT, since that is where this is called. Always-visible (disabled when there is
// nothing to scroll) keeps the client rect stable and the paint non-reentrant.
void updateScrollbars(EditorState* st) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = (std::max)(0, contentHeight(st) - 1);
    si.nPage = static_cast<UINT>((std::max)(0, clientH(st)));
    si.nPos = st->scrollY;
    SetScrollInfo(st->hwnd, SB_VERT, &si, TRUE);

    si.nMax = (std::max)(0, contentWidth(st) - 1);
    si.nPage = static_cast<UINT>((std::max)(0, clientW(st)));
    si.nPos = st->scrollX;
    SetScrollInfo(st->hwnd, SB_HORZ, &si, TRUE);
}

// Defined below (the parent-notification funnel). Forward-declared because the funnel's
// contract is "ANY change of scrollY/scrollX reports here, whatever caused it", and two of
// the paths that can change them — setFontInternal and d2dEditorUpdateDpi — are written
// above it. A contract that only holds for the callers that happen to sit downstream of
// the definition is not a contract.
void flushViewNotifications(EditorState* st);

// ---- font (one place, so EM_SETCHARFORMAT and d2dEditorSetFont cannot drift) -
void setFontInternal(EditorState* st, const std::wstring& face, float pointSize) {
    const std::wstring f = face.empty() ? st->face : face;
    const float p = (pointSize > 0.0f) ? pointSize : st->pointSize;
    // Only rebuild on a REAL change. styleEditor runs on every file open (MainWindow.cpp
    // :738) and every theme/settings flip (:1618), and an unconditional rebuild would drop
    // the whole layout cache and reset the horizontal high-water mark each time.
    if (f == st->face && p == st->pointSize) return;
    st->face = f;
    st->pointSize = p;
    SafeRelease(st->format);  // the em-size and every metric derived from it are stale
    st->lineH = 0;
    st->spaceW = 0;
    ensureFormat(st);
    dropLayoutCache(st);  // every cached layout was shaped at the old face/size
    st->maxLineW = 0;     // and so was the horizontal high-water mark
    clampScroll(st);      // a smaller font shrinks contentHeight, which can move scrollY
    InvalidateRect(st->hwnd, nullptr, FALSE);
    flushViewNotifications(st);  // ...and a moved scrollY is a stale gutter — see the funnel
}

// ---- the parent-notification funnel -----------------------------------------
// THE part of slice 3 that can lose a user's work, so it is written out at length.
//
// MainWindow's g.dirty is a pure function of (editorText(), g.savedText) and EN_CHANGE is
// only the trigger to RECOMPUTE it. A change this control fails to report therefore leaves
// g.dirty false and the buffer is discarded WITH NO PROMPT. confirmSaveIfDirty
// (MainWindow.cpp:758) re-derives as insurance, but four readers of g.dirty do not go
// through it: Ctrl+S / ID_SAVE (:1954), the Save button (:1661), the build auto-save
// (:1244) and the update-install auto-save (:2017). A missed notification makes Ctrl+S a
// silent no-op and lets WinSparkle restart the app with the last edit gone.
//
// SendMessageW, NEVER PostMessageW, and the reason cuts both ways. loadFileIntoEditor
// (MainWindow.cpp:736-740) and closeProject (:1441) set g.loadingFile, set the text and
// clear the flag again on one straight line with no message pump between: a SENT
// notification lands inside that window and is correctly suppressed, a POSTED one lands
// after it and is processed as a user edit — so the guard the codebase has relied on since
// phase 39 would silently stop working. In the other direction a posted notification for a
// REAL edit can be dispatched inside a modal's pump (every dialog here pumps with a null
// filter, SaveChangesDialog included) after confirmSaveIfDirty has already asked, be
// swallowed by the guard, and never be recomputed — there is no retry. Sending also means
// the host observes the post-edit state of the very edit that fired it. And EN_SELCHANGE
// cannot be posted at all: its lParam points at a stack SELCHANGE.
//
// WM_COMMAND for EN_CHANGE/EN_VSCROLL/EN_HSCROLL and WM_NOTIFY for EN_SELCHANGE is not a
// preference, it is RichEdit's OWN split — EN_CHANGE (0x0300), EN_HSCROLL (0x0601) and
// EN_VSCROLL (0x0602) are WinUser.h edit-control codes delivered by WM_COMMAND, while
// EN_SELCHANGE (0x0702) is a richedit code carrying a SELCHANGE payload and has no
// WM_COMMAND form (sent that way it falls out of MainWindow's WM_COMMAND handler
// unmatched, silently, and the Ln/Col readout freezes forever). Matching the split exactly
// meant the host ran the SAME branch for both editors while there were two, so any
// behavioural difference between them was attributable to the control and never to the
// dialect. Sending BOTH forms is wrong for a different reason: MainWindow handles each of
// them, so onEditChanged would run twice per keystroke.
//
// TRAP: MainWindow.cpp:1931 begins `if (lParam != 0 && ...)` to tell a control notification
// from a menu command, so lParam MUST be the control's HWND. A WM_COMMAND with lParam == 0
// is skipped in full, and that failure is indistinguishable from having no funnel at all.

// Raised while a notification is in flight, released by scope so an early return in the
// host cannot strand it — exactly the trap MainWindow's plain-bool g.highlighting had, and
// one reason slice 8 was able to delete that flag outright rather than port it.
struct NotifyScope {
    EditorState* st;
    explicit NotifyScope(EditorState* s) : st(s) { ++st->notifyDepth; }
    ~NotifyScope() { --st->notifyDepth; }
    NotifyScope(const NotifyScope&) = delete;
    NotifyScope& operator=(const NotifyScope&) = delete;
};

void notifyParentCommand(EditorState* st, UINT code) {
    HWND parent = GetParent(st->hwnd);
    if (!parent) return;  // standalone host (tests/d2d_render_test.cpp) — nothing to tell
    NotifyScope guard(st);
    SendMessageW(parent, WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(st->hwnd), code),
                 reinterpret_cast<LPARAM>(st->hwnd));
}

// The ONE place the host is told the text changed. Every model mutation reaches it through
// afterEdit(); nothing else may mutate the buffer.
void notifyChanged(EditorState* st) {
    if (!(st->eventMask & ENM_CHANGE)) return;
    // Deliberately NOT gated on notifyDepth. The host does not edit the buffer from inside
    // onEditChanged — it reads the text, the undo flags and the line geometry and nothing
    // else — so this cannot recurse, and an EN_CHANGE that can be suppressed by anything is
    // an EN_CHANGE that can be lost.
    notifyParentCommand(st, EN_CHANGE);
}

// Report a SETTLED view change: scroll first (the host repaints the line-number gutter off
// it), then the selection (the Ln/Col readout). Diff-based on purpose — scrollX, scrollY
// and the selection are written in a dozen places between them, and a per-site notify is a
// list that will eventually be got wrong; here an extra call is a no-op and a missing one
// only defers the report to the next.
//
// EN_VSCROLL/EN_HSCROLL are a deliberate SUPERSET of RichEdit, which documents them as
// firing for scrollbar interaction only — which is why arrowing off the bottom of the view
// can leave a stale gutter today. Any change of scrollY/scrollX reports here, whatever
// caused it. The consumer is one InvalidateRect of the gutter rect (MainWindow.cpp:1932),
// so over-sending costs a repaint and under-sending costs a wrong gutter.
void flushViewNotifications(EditorState* st) {
    if (st->notifyDepth > 0) return;  // re-entrant: leave the diff, report the NET change

    if (st->scrollX != st->notifiedScrollX) {
        st->notifiedScrollX = st->scrollX;
        if (st->eventMask & ENM_SCROLL) notifyParentCommand(st, EN_HSCROLL);
    }
    if (st->scrollY != st->notifiedScrollY) {
        st->notifiedScrollY = st->scrollY;
        if (st->eventMask & ENM_SCROLL) notifyParentCommand(st, EN_VSCROLL);
    }

    const editor::Selection sel = st->model.selection();
    if (sel == st->notifiedSel) return;
    // Gated on redraw, which is EXACT rather than heuristic: every programmatic selection
    // storm in the host sits inside a WM_SETREDRAW(FALSE) window (highlight
    // MainWindow's deleted highlight() — one EM_EXSETSEL per token, hundreds per keystroke;
    // clearErrorMarks :605-607; markErrorLines :633-646), and gotoLineCol (:1287), which
    // DOES want the status bar to move, is not. Not recording the value either is the
    // other half: the storm's net effect is a restore to the selection it started from, so
    // the flush at WM_SETREDRAW(TRUE) then correctly finds nothing to report.
    if (st->redrawOff > 0) return;
    st->notifiedSel = sel;
    if (!(st->eventMask & ENM_SELCHANGE)) return;
    HWND parent = GetParent(st->hwnd);
    if (!parent) return;
    SELCHANGE sc{};
    sc.nmhdr.hwndFrom = st->hwnd;
    sc.nmhdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(st->hwnd));
    sc.nmhdr.code = EN_SELCHANGE;
    // Normalised, never reversed and never negative: MainWindow.cpp:1925 computes
    // Col = cpMin - lineStart + 1 and would otherwise print a negative column.
    sc.chrg.cpMin = static_cast<LONG>(sel.min());
    sc.chrg.cpMax = static_cast<LONG>(sel.max());
    sc.seltyp = static_cast<WORD>(sel.empty() ? SEL_EMPTY : SEL_TEXT);
    NotifyScope guard(st);
    SendMessageW(parent, WM_NOTIFY, static_cast<WPARAM>(sc.nmhdr.idFrom),
                 reinterpret_cast<LPARAM>(&sc));
}

// ---- painting ---------------------------------------------------------------

// EVERYTHING this control draws, and the only place it is written. Takes the target as a
// parameter rather than reaching for st->rt so the same code produces the window's pixels
// and the offscreen WIC bitmap d2dEditorRenderToPng encodes — a test that rendered through
// a second, parallel drawing path would be testing the wrong code.
//
// Caller's contract: between BeginDraw and EndDraw, with the line index already current
// (ensureLineIndex) and the scroll clamped. The BRUSHES still come from `st`, so `rt` and
// st->br.text/brSelection/brCaret must belong to the same device — d2dEditorRenderToPng
// swaps the whole set together for exactly that reason.
void drawContent(EditorState* st, ID2D1RenderTarget* rt) {
    const Theme& th = currentTheme();
    // EM_SETBKGNDCOLOR wins when the host has set one; it only ever sets Theme::windowBg,
    // so this is the same pixel either way — reading it here is what keeps it that way.
    rt->Clear(colorToD2D(st->haveBkColor ? st->bkColor : th.windowBg));

    const float ox = static_cast<float>(dpx(st, kInsetX) - st->scrollX);
    const float oy = static_cast<float>(dpx(st, kInsetY) - st->scrollY);
    const float lh = (std::max)(1.0f, st->lineH);
    const float viewW = static_cast<float>(clientW(st));

    size_t first = 0, last = 0;
    visibleRange(st, first, last);
    const editor::Selection sel = st->model.selection();

    if (!st->br.text) return;
    for (size_t i = first; i <= last; ++i) {
        IDWriteTextLayout* layout = layoutForLine(st, i);
        if (!layout) continue;
        const float y = oy + static_cast<float>(i) * lh;
        const size_t a = st->lineStarts[i];
        const size_t b = lineEndOffset(st, i);

        // Error-line tint (slice 4), BEHIND everything else on the line. The band spans the
        // full client width, where RichEdit's CFM_BACKCOLOR only reaches as far as the
        // line's characters do — a deliberate difference (a band is what a code editor
        // draws), on the identical colour, which is the part that must not drift.
        if (st->br.errorTint && !st->errorLines.empty() &&
            std::binary_search(st->errorLines.begin(), st->errorLines.end(),
                               static_cast<int>(i))) {
            rt->FillRectangle(D2D1::RectF(0.0f, y, viewW, y + lh), st->br.errorTint);
        }

        // Selection band for this line's slice of the selection. A selection that runs
        // THROUGH the line break gets a trailing stub so a selected empty line is still
        // visible — otherwise it would be a zero-width rectangle.
        if (!sel.empty() && st->br.selection && sel.max() > a && sel.min() <= b) {
            const size_t sa = (std::max)(sel.min(), a);
            const size_t sb = (std::min)(sel.max(), b);
            float x1 = xInLine(st, i, sa);
            float x2 = xInLine(st, i, sb);
            if (sel.max() > b) x2 += st->spaceW;
            if (x2 > x1) rt->FillRectangle(D2D1::RectF(ox + x1, y, ox + x2, y + lh), st->br.selection);
        }

        // ---- syntax colouring (slice 4) --------------------------------------
        // ONE layout, drawn once per coloured run inside a clip covering that run's
        // x-range. Read the two rules at the top of this file before changing any of it:
        // no SetDrawingEffect (the layout must stay device-independent), and the same
        // layout for every run so painting and hit-testing share one set of advance widths
        // — a second, per-run layout could shape differently and drift the caret off the
        // glyphs. The clip is ALIASED so adjacent runs, which share a boundary x, round to
        // the same pixel and tile exactly: no seam and no double-drawn glyph edge.
        const std::wstring& t = st->model.text();
        const size_t n = b - a;
        editor::LexState ls = lineStartState(st, i);
        editor::computeSpans(t.c_str() + a, n, ls, st->spanScratch);
        // `ls` now holds the state for line i + 1 — hand it to the cache so the next
        // visible line is a hit instead of re-lexing this one to find the same answer.
        if (st->lexStateKnown == i + 1 && i + 1 < st->lineLexState.size()) {
            st->lineLexState[i + 1] = ls;
            st->lexStateKnown = i + 2;
        }

        const auto drawRun = [&](size_t from, size_t to, editor::SpanClass cls) {
            if (to <= from) return;
            ID2D1SolidColorBrush* br = brushForClass(st, cls);
            if (!br) br = st->br.text;
            if (from == 0 && to == n) {  // the whole line is one colour — no clip needed
                rt->DrawTextLayout(D2D1::Point2F(ox, y), layout, br, D2D1_DRAW_TEXT_OPTIONS_NONE);
                return;
            }
            const float rx1 = ox + xInLine(st, i, a + from);
            const float rx2 = ox + xInLine(st, i, a + to);
            if (rx2 <= 0.0f || rx1 >= viewW) return;  // this run is off-screen; skip the draw
            // The OUTER edges open out to the client edge so a glyph whose ink overhangs
            // its advance width is not shaved at the start or end of the line. Interior
            // boundaries stay exact, because that is where two runs meet.
            const float x1 = (from == 0) ? 0.0f : rx1;
            const float x2 = (to == n) ? viewW : rx2;
            rt->PushAxisAlignedClip(D2D1::RectF(x1, 0.0f, x2, static_cast<float>(clientH(st))), D2D1_ANTIALIAS_MODE_ALIASED);
            rt->DrawTextLayout(D2D1::Point2F(ox, y), layout, br, D2D1_DRAW_TEXT_OPTIONS_NONE);
            rt->PopAxisAlignedClip();
        };

        size_t pos = 0;
        for (const editor::Span& sp : st->spanScratch) {
            drawRun(pos, sp.begin, editor::SpanClass::Default);  // the gap before it
            drawRun(sp.begin, sp.end, sp.cls);
            pos = sp.end;
        }
        drawRun(pos, n, editor::SpanClass::Default);  // ...and the tail (the whole line if
                                                     // there were no spans at all)
    }

    // THE DROP CARET (slice 7) takes the ordinary caret's place while a drag hovers, and
    // the `else` is the point: a local drag would otherwise show TWO carets, the blinking
    // insertion point at the end of the source selection and the drop position, with no
    // way to tell which one the text is about to land at.
    //
    // Three differences from the caret below, all deliberate: it ignores hasFocus (a drag
    // from another application has not given us the focus and will not until the drop), it
    // ignores caretOn (a position readout that blinks is a position readout you have to
    // wait for), and it is twice as wide so it reads as a target rather than as the caret
    // having jumped. The offset is re-clamped here rather than trusted: it was computed
    // when the mouse last moved, and the host can edit the buffer from under a drag —
    // DoDragDrop pumps messages.
    if (st->dropCaret && st->br.caret) {
        const size_t off = (std::min)(st->dropOffset, st->model.length());
        const size_t dl = lineIndexForOffset(st, off);
        const float dx = ox + xInLine(st, dl, off);
        const float dy = oy + static_cast<float>(dl) * lh;
        const float w = static_cast<float>((std::max)(2, dpx(st, 2)));
        rt->FillRectangle(D2D1::RectF(dx, dy, dx + w, dy + lh), st->br.caret);
    } else if (st->hasFocus && st->caretOn && st->br.caret) {
        const size_t cl = lineIndexForOffset(st, st->model.caret());
        const float cx = ox + xInLine(st, cl, st->model.caret());
        const float cy = oy + static_cast<float>(cl) * lh;
        const float w = static_cast<float>((std::max)(1, dpx(st, 1)));
        rt->FillRectangle(D2D1::RectF(cx, cy, cx + w, cy + lh), st->br.caret);
    }
}

// ---- the SYSTEM caret (slice 5) ---------------------------------------------
// This control PAINTS its caret (drawContent, above) so the theme owns that pixel. What
// that silently dropped is the SYSTEM caret, and the system caret is not decoration: it is
// what GetGUIThreadInfo / the OBJID_CARET accessible object report, so with none created
// there is NOTHING for Narrator, Windows Magnifier's "follow the text cursor", or an IME
// placing its candidate list to track. RichEdit creates one, so the Direct2D editor was
// shipping a real accessibility regression against the control it replaces — banked for
// slice 6 in the handover, bought here instead because slice 5 offers this to users.
//
// So: a REAL caret, created on focus and destroyed on kill-focus, moved with SetCaretPos —
// but deliberately NEVER ShowCaret'd. A shown caret is a SECOND caret: a blinking GDI bar
// beside the painted one, drawn by USER32 into a client area that a Direct2D swap chain
// re-presents underneath it. Created-and-positioned is all the reporting APIs read;
// visible is not part of the contract, and this control's visible caret is the one it draws.

// The caret in CLIENT coordinates. Mirrors drawContent's ox/oy exactly — the same
// inset-minus-scroll origin and the same line * lineH — in the ints SetCaretPos and
// ImmSetCompositionWindow take. ONE copy of that arithmetic, so the painted caret, the
// system caret and the IME composition window cannot end up in three different places.
// The float expressions are drawContent's and EM_POSFROMCHAR's, character for character,
// and that is deliberate rather than tidy: `(LONG)(n + f)` and `n + (LONG)f` disagree by
// one pixel whenever n + f is negative (truncation is toward zero, not down), so the
// integer-first form this used to carry could put the IME window a pixel off the glyph on a
// scrolled-away caret. Written this way, EM_POSFROMCHAR(caret) IS the caret position, which
// is also what d2d_dialect case 11 asserts.
void caretClientPos(EditorState* st, POINT& pt, int& h) {
    ensureFormat(st);  // lineH, or the caret is one pixel tall at the top of the window
    ensureLineIndex(st);
    const size_t line = lineIndexForOffset(st, st->model.caret());
    const float lh = (std::max)(1.0f, st->lineH);
    pt.x = static_cast<LONG>(static_cast<float>(dpx(st, kInsetX) - st->scrollX) +
                             xInLine(st, line, st->model.caret()));
    pt.y = static_cast<LONG>(static_cast<float>(dpx(st, kInsetY) - st->scrollY) +
                             static_cast<float>(line) * lh);
    h = (std::max)(1, static_cast<int>(lh));
}

// Called from the tail of paint(), which is the one place that cannot drift from where the
// caret was actually drawn: every path that moves the caret or the view already ends in an
// InvalidateRect, and a path that does not repaint has not moved the painted caret either.
void syncSystemCaret(EditorState* st) {
    if (!st->sysCaret) return;
    POINT pt{};
    int h = 0;
    caretClientPos(st, pt, h);
    if (h != st->sysCaretH) {
        // CreateCaret is the only way to change a caret's SIZE, so a font or DPI change has
        // to rebuild it. It also destroys whatever caret existed, which is exactly right.
        if (!CreateCaret(st->hwnd, nullptr, (std::max)(1, dpx(st, 1)), h)) return;
        st->sysCaretH = h;
        st->sysCaretPt = POINT{ pt.x - 1, pt.y };  // never equal: force the SetCaretPos below
    }
    if (pt.x == st->sysCaretPt.x && pt.y == st->sysCaretPt.y) return;
    st->sysCaretPt = pt;
    SetCaretPos(pt.x, pt.y);
}

// The editor with no Direct2D device has nothing to draw with, so this draws with GDI,
// which needs none. Shown only after the failure has outlived kDeviceFailQuietMs — see the
// call site. The wording's job is to say the text is not lost, because the visible evidence
// says the opposite.
void drawDeviceFailureNotice(EditorState* st, HDC hdc) {
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    const Theme& th = currentTheme();
    if (HBRUSH bg = CreateSolidBrush(st->haveBkColor ? st->bkColor : th.windowBg)) {
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
    }
    HFONT f = CreateFontW(-dpx(st, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ oldFont = f ? SelectObject(hdc, f) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, th.textPrimary);
    RECT tr = rc;
    InflateRect(&tr, -dpx(st, 24), -dpx(st, 24));
    DrawTextW(hdc,
              L"The editor could not start Direct2D, so it has nothing to draw with.\n\n"
              L"It is retrying four times a second. Your text is still here and unchanged — "
              L"this is a drawing failure, not a lost buffer — and the file on disk is "
              L"exactly as you last saved it.\n\n"
              L"If this does not clear, update the graphics driver or restart Sentinel-IDE.",
              -1, &tr, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    if (oldFont) SelectObject(hdc, oldFont);
    if (f) DeleteObject(f);
}

void paint(EditorState* st) {
    PAINTSTRUCT ps;
    BeginPaint(st->hwnd, &ps);
    // Device loss is why the InvalidateRect calls below exist: BeginPaint has already
    // VALIDATED the update region, and losing the device does not dirty the window, so
    // without them the editor sits on stale pixels until something else repaints it. While
    // focused the blink timer masks that within ~530ms, which is why it survives casual
    // testing. (The reference has the same omission; a code editor lives far longer than a
    // query box.) Re-entering is safe -- ensureDeviceResources is idempotent.
    if (ensureDeviceResources(st)) {
        if (st->deviceFailSince) {
            if (st->deviceFailReported) logMsg(LogLevel::Info, L"Editor: the Direct2D device came back");
            st->deviceFailSince = 0;
            st->deviceFailReported = false;
        }
        ensureLineIndex(st);
        clampScroll(st);
        updateScrollbars(st);
        // The horizontal extent is only learned by LAYING lines out, which happens below,
        // so the bar set above is one paint stale whenever a wider line just came into
        // view. Re-set it after the draw if the high-water mark moved (see the tail of
        // this function) rather than leaving a scrollbar that under-reports the document.
        const float maxWBefore = st->maxLineW;

        // INVARIANT for the draw below: linesDirty is false from here to EndDraw, so
        // nothing inside can rebuild the line index. That matters because drawContent's loop
        // holds a raw IDWriteTextLayout* across xInLine calls, and xInLine -> layoutForLine ->
        // ensureLineIndex would dropLayoutCache and RELEASE the pointer still in hand --
        // heap corruption during paint, and rare enough to be miserable to find. It is safe
        // today only because ensureLineIndex ran above and no edit can arrive mid-paint.
        // Slice 3 (a change notification) and slice 4 (a highlighter callback) are precisely
        // the changes that can break this; re-check it then. No assert: builds are Release
        // with NDEBUG since phase 44, so an assert here would compile to nothing.
        //
        // RE-CHECKED FOR SLICE 4, and it still holds. The colouring added two calls inside
        // the loop and NEITHER can rebuild the index: lineStartState calls ensureLineIndex,
        // which is a no-op while linesDirty is false (it is — set only by afterEdit, and no
        // edit can arrive mid-paint), and then only reads model.text() and writes the
        // lineLexState prefix; computeSpans is pure and touches no D2D or DWrite object at
        // all. The colouring is also the one thing here that must NEVER call back into the
        // model, which is why the lexer takes a (pointer, length) rather than the control.
        //
        // RE-CHECKED FOR SLICE 3, and it still holds, for two reasons that must both stay
        // true. (a) The funnel never fires from inside paint: notifyChanged is only called
        // by afterEdit, and flushViewNotifications only by afterEdit/caretMoved/onScroll/
        // the wheel handlers/onAutoScroll/EM_EXSETSEL/EM_SCROLLCARET/WM_SETREDRAW/WM_SIZE/
        // setFontInternal/d2dEditorUpdateDpi — none of which paint() reaches. (The last
        // three are the ones that make the funnel's "any change of scrollY/scrollX reports
        // here" literally true; they all come from the host, never from a paint.)
        // (b) Every message the host sends back from inside a
        // notification is non-mutating of the index: EM_GETTEXTLENGTHEX, EM_GETTEXTEX,
        // EM_EXGETSEL, EM_EXSETSEL, EM_SETCHARFORMAT, EM_GETLINECOUNT, EM_LINEINDEX,
        // EM_CANUNDO, EM_CANREDO, WM_SETREDRAW. EM_EXSETSEL is the one to watch: it goes to
        // EditorModel::setSelection and InvalidateRect and NOTHING else.
        st->rt->BeginDraw();
        drawContent(st, st->rt);
        if (st->rt->EndDraw() == D2DERR_RECREATE_TARGET) {
            discardDeviceResources(st);
            InvalidateRect(st->hwnd, nullptr, FALSE);  // D2D dropped the frame; ask for another
        }

        // Give back everything well outside the window. Without this the cache would grow
        // to one layout per line ever scrolled past — correct, but unbounded. Recomputing
        // the visible range here rather than threading it out of drawContent is free: it is
        // arithmetic over lineStarts, with no layout and no device involved.
        size_t first = 0, last = 0;
        visibleRange(st, first, last);
        const size_t keepLo = (first > kCacheMargin) ? first - kCacheMargin : 0;
        const size_t keepHi = (std::min)(lineCount(st), last + 1 + kCacheMargin);
        trimCache(st, keepLo, keepHi);

        if (st->maxLineW > maxWBefore) updateScrollbars(st);
    } else {
        // Retry on a timer, NOT with a bare InvalidateRect -- see kDeviceRetryTimer.
        SetTimer(st->hwnd, kDeviceRetryTimer, kDeviceRetryMs, nullptr);
        // NOTHING WAS DRAWN, and Direct2D is the only thing that ever draws this control, so
        // the client area keeps whatever was last on it -- at cold start, nothing. A brief
        // loss recovers on the timer above and must not flash an error; one that outlives
        // kDeviceFailQuietMs is not brief, and since slice 8 there is no RichEdit editor to
        // fall back to, so an unexplained blank pane is the one outcome to refuse. Say so in
        // GDI (which needs no device) and log it once per episode.
        const ULONGLONG now = GetTickCount64();
        if (!st->deviceFailSince) st->deviceFailSince = now;
        if (now - st->deviceFailSince >= kDeviceFailQuietMs) {
            if (!st->deviceFailReported) {
                st->deviceFailReported = true;
                logMsg(LogLevel::Error,
                       L"Editor: no Direct2D device after " + std::to_wstring(kDeviceFailQuietMs) +
                       L"ms - showing the fallback notice and retrying every " +
                       std::to_wstring(kDeviceRetryMs) + L"ms");
            }
            drawDeviceFailureNotice(st, ps.hdc);
        }
        // Trimming must NOT be conditional on having painted. While the device is
        // unobtainable every WM_PAINT bails out above, but caret movement and hit-testing
        // keep laying single lines out through layoutForLine -- so holding Down through a
        // 20,000-line file in that state would build 20,000 live layouts with nothing ever
        // reclaiming them. Trim around the caret instead; it needs no device.
        ensureLineIndex(st);
        const size_t cl = lineIndexForOffset(st, st->model.caret());
        const size_t lo = (cl > kCacheMargin) ? cl - kCacheMargin : 0;
        const size_t hi = (std::min)(lineCount(st), cl + 1 + kCacheMargin);
        trimCache(st, lo, hi);
    }
    // AFTER both branches: the system caret follows the PAINTED caret, including when the
    // device is lost and nothing was drawn (the insertion point is still where it is, and
    // an assistive tool asking about it must not be told a stale answer). The one layout
    // this can build in the lost branch is the caret's own line, which the trim above
    // keeps anyway.
    syncSystemCaret(st);
    EndPaint(st->hwnd, &ps);
}

// ---- post-edit / caret-move bookkeeping -------------------------------------

// EVERY model mutation ends here — typing, Enter, Tab, Backspace/Delete (plain and by
// word), cut, paste, IME commit, EM_UNDO/EM_REDO, WM_SETTEXT, and since slice 7 DROP (both
// halves: the text a drop inserts, and the delete at the end of a drag-move out to another
// application). That is by construction, not by inspection: nothing else in this file
// touches st->model's text, so the notification below cannot be forgotten by slice 7's
// successors either.
//
// The drop path is the one that had to be threaded in deliberately rather than inherited.
// g.dirty is a pure function of (editorText(), g.savedText) and EN_CHANGE is only the
// trigger to recompute it, so a drop that mutated the buffer and returned to OLE without
// coming through here would leave the buffer DISCARDABLE WITH NO PROMPT — this project's
// defect #1, arriving through a code path nobody types into. applyDrop therefore ends in
// this function and nowhere else.
void afterEdit(EditorState* st) {
    st->linesDirty = true;
    st->desiredX = -1;
    st->caretOn = true;
    ensureLineIndex(st);  // text changed -> the index must be current for caret geometry
    ensureCaretVisible(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
    // ORDER IS THE CONTRACT, and EN_CHANGE goes LAST. onEditChanged (MainWindow.cpp:666)
    // re-enters this control SYNCHRONOUSLY — EM_GETTEXTLENGTHEX + EM_GETTEXTEX for the
    // dirty comparison, EM_CANUNDO/EM_CANREDO for the toolbar — and its InvalidateRect of
    // the gutter makes the parent ask EM_GETFIRSTVISIBLELINE / EM_LINEINDEX /
    // EM_POSFROMCHAR on its next paint. Notify before the line index is rebuilt or the
    // scroll has settled and the host reads a half-updated editor. Never from inside
    // paint(): drawContent holds a raw IDWriteTextLayout* across xInLine calls (see the
    // invariant there), and none of the messages the host sends back drop that cache.
    flushViewNotifications(st);
    notifyChanged(st);
}

void caretMoved(EditorState* st, bool resetDesiredX) {
    if (resetDesiredX) st->desiredX = -1;
    st->caretOn = true;
    ensureCaretVisible(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
    flushViewNotifications(st);  // EN_SELCHANGE (+ EN_VSCROLL if the view followed), never EN_CHANGE
}

// The ONE path that replaces the whole buffer, so WM_SETTEXT and d2dEditorSetText cannot
// drift into two copies of it — and, more to the point, so neither can become a silent text
// replacement that skips the funnel. RichEdit raises EN_CHANGE for WM_SETTEXT (which is
// exactly why g.loadingFile exists), and both of the host's call sites (MainWindow.cpp:737,
// :1441) bracket it with that guard and then write the post-conditions by hand — so it is
// provably suppressed there either way. It fires for fidelity, and so that a third
// SetWindowTextW added later is covered without anyone remembering to cover it.
void setTextInternal(EditorState* st, const std::wstring& s) {
    st->model.setText(s);          // also clears undo/redo, as RichEdit's WM_SETTEXT does:
    st->model.setCaret(0, false);  // loadFileIntoEditor:744 relies on that ("SetWindowText
    st->scrollX = 0;               // cleared the undo buffer") when it resets the toolbar.
    st->scrollY = 0;
    st->pendingHigh = 0;
    st->maxLineW = 0;  // every measured width belongs to the old document
    // ERROR TINTS DIE WITH THE DOCUMENT. errorLines holds ABSOLUTE line numbers, so leaving
    // them across a whole-document replacement paints red bands at those line numbers in an
    // innocent file — and MainWindow's loadFileIntoEditor sets g.errorMarks = false without
    // calling clearErrorMarks, so nothing downstream could ever clear them either; only the
    // next build would. RichEdit cannot have this bug: its tint is a CFM_BACKCOLOR character
    // format, so it dies with the text automatically. Painted decoration has to be told.
    st->errorLines.clear();
    afterEdit(st);
}

// Vertical navigation: index arithmetic, because visual line == logical line.
size_t verticalTarget(EditorState* st, int dirLines) {
    ensureLineIndex(st);
    const size_t cur = lineIndexForOffset(st, st->model.caret());
    if (st->desiredX < 0) st->desiredX = xInLine(st, cur, st->model.caret());
    const long long t = static_cast<long long>(cur) + dirLines;
    if (t < 0) return st->model.docStart();  // above the first line -> document start
    if (t >= static_cast<long long>(lineCount(st)))
        return st->model.docEnd();  // below the last -> document end
    return offsetFromXInLine(st, static_cast<size_t>(t), st->desiredX);
}

void pageMove(EditorState* st, int dir, bool shift) {
    const int lines =
        (std::max)(1, static_cast<int>(static_cast<float>(clientH(st)) /
                                       (std::max)(1.0f, st->lineH)) - 1);
    st->model.setCaret(verticalTarget(st, dir * lines), shift);
    caretMoved(st, false);
}

size_t offsetFromPoint(EditorState* st, int mx, int my) {
    ensureLineIndex(st);
    const float lh = (std::max)(1.0f, st->lineH);
    const float ly = static_cast<float>(my) - static_cast<float>(dpx(st, kInsetY) - st->scrollY);
    long long li = static_cast<long long>(std::floor(ly / lh));
    if (li < 0) li = 0;
    if (li >= static_cast<long long>(lineCount(st))) li = static_cast<long long>(lineCount(st)) - 1;
    const float lx = static_cast<float>(mx) - static_cast<float>(dpx(st, kInsetX) - st->scrollX);
    return offsetFromXInLine(st, static_cast<size_t>(li), lx);
}

// ---- clipboard (carried over verbatim) --------------------------------------

// Returns false if the text did NOT reach the clipboard. Cut depends on that: OpenClipboard
// fails transiently whenever another process holds the clipboard open (clipboard managers and
// RDP clipboard sync do it constantly), and a cut that deletes anyway destroys the selection
// with nothing to paste back. Undo recovers it, but "unsaved edits silently discarded" is
// this project's shipped defect #1 and it is not being reintroduced through the back door.
bool copyToClipboard(EditorState* st) {
    const auto sel = st->model.selection();
    if (sel.empty()) return false;
    const std::wstring s = st->model.text().substr(sel.min(), sel.max() - sel.min());
    if (!OpenClipboard(st->hwnd)) return false;
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    // Allocate BEFORE EmptyClipboard: emptying first would wipe the user's clipboard on an
    // allocation failure, losing content this editor never owned.
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        CloseClipboard();
        return false;
    }
    bool ok = false;
    if (void* p = GlobalLock(h)) {
        std::memcpy(p, s.c_str(), bytes);
        GlobalUnlock(h);
        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, h)) {
            ok = true;  // the clipboard owns h now
        } else {
            GlobalFree(h);  // ownership stayed ours
        }
    } else {
        GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

void cutToClipboard(EditorState* st) {
    if (copyToClipboard(st)) st->model.deleteSelection();
}

void pasteFromClipboard(EditorState* st) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
    if (!OpenClipboard(st->hwnd)) return;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            st->model.insertText(editor::normalizeNewlines(p));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

// ---- double-click word selection (carried over) -----------------------------

void selectWordAt(EditorState* st, size_t off) {
    const std::wstring& t = st->model.text();
    size_t a = off, b = off;
    if (off < t.size() && isWordChar(t[off])) {
        while (a > 0 && isWordChar(t[a - 1])) --a;
        while (b < t.size() && isWordChar(t[b])) ++b;
    } else if (off > 0 && isWordChar(t[off - 1])) {
        while (a > 0 && isWordChar(t[a - 1])) --a;
        b = off;
    } else {
        st->model.setCaret(off, false);
        return;
    }
    st->model.setSelection(a, b);
}

// ---- caret blink (carried over) ---------------------------------------------

void startBlink(EditorState* st) {
    const UINT bt = GetCaretBlinkTime();
    if (bt == 0 || bt == INFINITE) {
        st->caretOn = true;  // blinking disabled by the user
        return;
    }
    st->blinkTimer = SetTimer(st->hwnd, kBlinkTimer, bt, nullptr);
}

void stopBlink(EditorState* st) {
    if (st->blinkTimer) {
        KillTimer(st->hwnd, kBlinkTimer);
        st->blinkTimer = 0;
    }
}

// ---- IME (carried over; caret geometry is the per-line version) -------------

void positionImeWindow(EditorState* st) {
    HIMC himc = ImmGetContext(st->hwnd);
    if (!himc) return;
    // Same arithmetic as the painted caret and the system caret, from the one copy of it.
    // (It used to be spelled out here and skipped ensureFormat, so it was correct only
    // because something else had already measured the font — see caretClientPos.)
    POINT pt{};
    int h = 0;
    caretClientPos(st, pt, h);
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = pt;
    ImmSetCompositionWindow(himc, &cf);
    ImmReleaseContext(st->hwnd, himc);
}

// ---- input (surrogate buffering carried over verbatim) ----------------------

void onChar(EditorState* st, wchar_t c) {
    std::wstring s;
    if (IS_HIGH_SURROGATE(c)) {
        st->pendingHigh = c;
        return;
    }
    if (IS_LOW_SURROGATE(c)) {
        if (!st->pendingHigh) return;  // stray low surrogate
        s.push_back(st->pendingHigh);
        s.push_back(c);
        st->pendingHigh = 0;
    } else {
        st->pendingHigh = 0;
        if (c == L'\r')
            s = L"\n";
        else if (c == L'\t')
            s = L"\t";
        else if (c < 0x20)
            return;  // control chars are handled in WM_KEYDOWN
        else
            s.push_back(c);
    }
    st->model.insertText(s);
    afterEdit(st);
}

// Returns true if the key was handled.
bool onKeyDown(EditorState* st, WPARAM vk) {
    // TRAP: AltGr arrives as Ctrl+Alt, so a bare Ctrl test fires on ORDINARY TYPING on
    // every AltGr layout (Polish, German, Czech...). It is not cosmetic: AltGr+A on the
    // Polish layout would run selectAll, and the WM_CHAR for the accented letter is ALREADY
    // queued by TranslateMessage -- swallowing this WM_KEYDOWN cannot stop it -- so the next
    // insertText replaces the whole document with one character. AltGr+X cuts it away first.
    // RichEdit (what ships today) handles AltGr correctly, so this would be a REGRESSION at
    // slice 6. Excluding Alt here fixes every ctrl case below at once, word navigation too.
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0 && !alt;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (vk) {
        case VK_LEFT: st->model.moveLeft(shift, ctrl); caretMoved(st, true); return true;
        case VK_RIGHT: st->model.moveRight(shift, ctrl); caretMoved(st, true); return true;
        case VK_UP:
            st->model.setCaret(verticalTarget(st, -1), shift);
            caretMoved(st, false);
            return true;
        case VK_DOWN:
            st->model.setCaret(verticalTarget(st, +1), shift);
            caretMoved(st, false);
            return true;
        case VK_PRIOR: pageMove(st, -1, shift); return true;
        case VK_NEXT: pageMove(st, +1, shift); return true;
        case VK_HOME:
            if (ctrl)
                st->model.moveDocStart(shift);
            else
                st->model.moveLineHome(shift);
            caretMoved(st, true);
            return true;
        case VK_END:
            if (ctrl)
                st->model.moveDocEnd(shift);
            else
                st->model.moveLineEnd(shift);
            caretMoved(st, true);
            return true;
        case VK_BACK:
            if (ctrl)
                st->model.deleteWordLeft();
            else
                st->model.backspace();
            afterEdit(st);
            return true;
        case VK_DELETE:
            if (shift)
                cutToClipboard(st);
            else if (ctrl)
                st->model.deleteWordRight();
            else
                st->model.deleteForward();
            afterEdit(st);
            return true;
        case 'A':
            if (ctrl) {
                st->model.selectAll();
                caretMoved(st, true);
                return true;
            }
            return false;
        case 'C':
            if (ctrl) {
                copyToClipboard(st);
                return true;
            }
            return false;
        case 'X':
            if (ctrl) {
                cutToClipboard(st);
                afterEdit(st);
                return true;
            }
            return false;
        case 'V':
            if (ctrl) {
                pasteFromClipboard(st);
                afterEdit(st);
                return true;
            }
            return false;
        case 'Z':
            if (ctrl) {
                if (shift) {
                    if (st->model.canRedo()) {
                        st->model.redo();
                        afterEdit(st);
                    }
                } else if (st->model.canUndo()) {
                    st->model.undo();
                    afterEdit(st);
                }
                return true;
            }
            return false;
        case 'Y':
            if (ctrl) {
                if (st->model.canRedo()) {
                    st->model.redo();
                    afterEdit(st);
                }
                return true;
            }
            return false;
        case VK_INSERT:
            if (ctrl) {
                copyToClipboard(st);
                return true;
            }
            if (shift) {
                pasteFromClipboard(st);
                afterEdit(st);
                return true;
            }
            return false;
        default: return false;
    }
}

// ---- OLE drag and drop (slice 7) --------------------------------------------
//
// THE LAST DIFFERENCE between this control and the RichEdit one it replaces: dragging TEXT
// within a file. Closing it is what makes the two feature-identical, which is the stated
// precondition for deleting the RichEdit path.
//
// FOUR THINGS HERE ARE LOAD-BEARING, and each of them is a real defect if it is got wrong.
//
// 1. REGISTERING A DROP TARGET INTERCEPTS FILE DROPS, and that would be a WORSE regression
//    than the one being fixed. Before this, the editor had no drop target, so a .sentinel
//    dropped on the text area fell through to the main window's WM_DROPFILES -> requestOpenPath,
//    which posts and lands behind confirmSaveIfDirty. An OLE drop target on the child wins
//    outright over the parent's DragAcceptFiles, so from the moment RegisterDragDrop
//    succeeds the shell stops delivering that message for the editor's rectangle. Handled
//    explicitly: CF_HDROP is checked FIRST, never treated as text, and forwarded to the
//    host's own handler so the guarded path stays the only opening path. See
//    forwardFilesToHost.
//
// 2. A MOVE IS ONE UNDO STEP. Delete-then-insert spelled as two model calls is two
//    snapshots, and the intermediate one is a half-move: Ctrl+Z would leave the text
//    deleted and not re-inserted. EditorModel::moveSelectionTo does both under one
//    recordPreEdit.
//
// 3. EVERY MUTATION GOES THROUGH afterEdit. A drop is a buffer edit that no keystroke
//    caused, which makes it exactly the kind of edit a notification funnel gets forgotten
//    for. applyDrop is the single mutating function here and it ends in afterEdit, i.e. in
//    a SYNCHRONOUS EN_CHANGE. Never PostMessage.
//
// 4. DoDragDrop IS A MODAL LOOP INSIDE OUR WINDOW PROC. It pumps messages, so while it is
//    on the stack this control can paint, tick its timers and — because the host posts work
//    to itself — even have its buffer replaced under it. So: the dragged text is COPIED out
//    of the model before the call (a const wchar_t* into model.text() would dangle across a
//    reallocation), the local move is performed by the DROP against the model's state at
//    that instant rather than against offsets captured at drag start, the delete at the end
//    of an external move re-checks that the range it is about to remove still holds the
//    text that was handed over, and the state pointer is re-validated after the call in
//    case the window went away with it. The blink timer is stopped for the duration and the
//    autoscroll timer is killed (st->selecting is cleared, which is also what makes
//    onAutoScroll a no-op if a WM_TIMER queued before the drag arrives during it).

// Where a drop would land, and whether it is even allowed. Split out so DragOver's preview
// and Drop's commit cannot disagree about the answer — a target that highlights a position
// it then refuses is a target the user cannot predict.
DWORD dropEffectFor(DWORD keyState, DWORD allowed, bool rejected) {
    // Dropping a selection ONTO ITSELF: refused outright, for a move AND for a Ctrl-copy.
    // A move there is a no-op that would still burn an undo step; a copy there would
    // silently duplicate text inside the run the user was pointing at. RichEdit refuses
    // both, and "no drop" cursor is the honest answer for a gesture that changes nothing.
    if (rejected) return DROPEFFECT_NONE;
    // Ctrl copies, everything else moves — the Windows text convention, and the one the
    // Settings hint has been promising. Fall back the other way when the source will not
    // allow what was asked for (a read-only source offers COPY only).
    DWORD want = (keyState & MK_CONTROL) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
    if (!(allowed & want)) want = (want == DROPEFFECT_MOVE) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
    return (allowed & want) ? want : DROPEFFECT_NONE;
}

// THE ONE PLACE A DROP MUTATES THE BUFFER. Returns false when nothing changed, so the
// caller reports DROPEFFECT_NONE and no source deletes anything on the strength of it.
//
// `localSource` means the payload came out of THIS control (see sentinelDragFormat), which
// is what separates "move my own selection" from "insert someone else's text".
bool applyDrop(EditorState* st, const std::wstring& text, size_t at, bool localSource, bool move) {
    const editor::Selection sel = st->model.selection();
    const bool inside = localSource && !sel.empty() && at >= sel.min() && at <= sel.max();
    if (inside) return false;  // belt and braces: DragOver already refused this
    if (localSource && move) {
        if (sel.empty()) return false;
        // Against the selection as it is NOW, not against offsets remembered at drag start.
        // The drag's modal loop could have moved them; this cannot be stale by construction.
        st->model.moveSelectionTo(at);
    } else {
        if (text.empty()) return false;
        // setCaret collapses the selection first, so an existing selection is left alone
        // rather than replaced — dropping text is an INSERT at a point, not a paste over.
        st->model.setCaret(at, false);
        const std::wstring ins = editor::normalizeNewlines(text);
        st->model.insertText(ins);
        // SELECT WHAT WAS JUST DROPPED, exactly as the move path does. Two reasons, and the
        // first is a real behavioural difference from the editor this one replaces:
        //
        //   1. RichEdit selects the inserted run, so the next keystroke REPLACES it. Leaving
        //      the caret after it instead means that keystroke APPENDS — measured on the same
        //      gesture in both editors: Ctrl-copy-drop "a" onto "abcdef" then type Z gives
        //      "abcdefZ" on RichEdit and "abcdefaZ" here. Slice 8 deleted the RichEdit path
        //      on the grounds that the two are feature-identical, so divergences like this
        //      have to be closed, not inherited.
        //   2. It also makes the drop cheap to correct — the run can be dragged straight on
        //      again — which is the reason EditorModel::moveSelectionTo already does it. The
        //      copy path simply did not, which was an inconsistency rather than a decision.
        //
        // This ALSO closes the undo-granularity hole: insertText opens a coalescing typing
        // run for a single character inserted into an empty selection, so a ONE-CHARACTER
        // copy-drop left that run open and the next typed character folded into the drop's
        // undo step — one Ctrl+Z would then undo both. Leaving a non-empty selection behind
        // ends the run, so the next keystroke starts its own step.
        const size_t at2 = st->model.caret();
        if (!ins.empty() && at2 >= ins.size()) st->model.setSelection(at2 - ins.size(), at2);
    }
    afterEdit(st);  // the funnel. Synchronous EN_CHANGE, or the buffer is discardable.
    return true;
}

// ---- the data object the drag hands out -------------------------------------
// A minimal IDataObject over one immutable string. TWO formats, and both are needed:
// CF_UNICODETEXT is what every other application reads, and the private
// sentinelDragFormat() carries the source HWND so our own drop target can recognise its
// own text. CF_TEXT is deliberately NOT offered — an ANSI down-conversion would mangle
// exactly the non-ASCII identifiers this editor's word predicate exists to handle, and
// every drop target this century reads CF_UNICODETEXT.
struct FormatEnum final : public IEnumFORMATETC {
    FormatEnum(std::vector<FORMATETC> f, ULONG at) : fmts_(std::move(f)), at_(at) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* out, ULONG* fetched) override {
        if (!out && count) return E_POINTER;
        ULONG n = 0;
        while (n < count && at_ < fmts_.size()) out[n++] = fmts_[at_++];
        if (fetched) *fetched = n;
        return (n == count) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const ULONG left = static_cast<ULONG>(fmts_.size()) - at_;
        at_ += (std::min)(count, left);
        return (count <= left) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        at_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = new (std::nothrow) FormatEnum(fmts_, at_);
        return *out ? S_OK : E_OUTOFMEMORY;
    }

    // PUBLIC destructor, not the usual COM-private one: `new (std::nothrow) FormatEnum`
    // outside the class potentially invokes it, so a private one does not compile.
    ~FormatEnum() = default;

private:
    LONG ref_ = 1;
    std::vector<FORMATETC> fmts_;
    ULONG at_ = 0;
};

struct EditorDataObject final : public IDataObject {
    EditorDataObject(std::wstring text, HWND owner) : text_(std::move(text)), owner_(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fe, STGMEDIUM* med) override {
        if (!fe || !med) return E_INVALIDARG;
        *med = STGMEDIUM{};
        if (!(fe->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        const void* bytes = nullptr;
        size_t n = 0;
        UINT64 hwndValue = 0;
        if (fe->cfFormat == CF_UNICODETEXT) {
            bytes = text_.c_str();
            n = (text_.size() + 1) * sizeof(wchar_t);  // the NUL travels with it
        } else if (sentinelDragFormat() && fe->cfFormat == sentinelDragFormat()) {
            hwndValue = static_cast<UINT64>(reinterpret_cast<UINT_PTR>(owner_));
            bytes = &hwndValue;
            n = sizeof(hwndValue);
        } else {
            return DV_E_FORMATETC;
        }
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
        if (!h) return E_OUTOFMEMORY;
        void* p = GlobalLock(h);
        if (!p) {
            GlobalFree(h);
            return E_OUTOFMEMORY;
        }
        std::memcpy(p, bytes, n);
        GlobalUnlock(h);
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = h;
        med->pUnkForRelease = nullptr;  // the RECEIVER frees it, via ReleaseStgMedium
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fe) override {
        if (!fe) return E_INVALIDARG;
        if (!(fe->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (fe->cfFormat == CF_UNICODETEXT) return S_OK;
        if (sentinelDragFormat() && fe->cfFormat == sentinelDragFormat()) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;  // required even on the E_NOTIMPL path
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (dir != DATADIR_GET) return E_NOTIMPL;  // nothing can be set on this object
        std::vector<FORMATETC> f;
        f.push_back(FORMATETC{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL });
        if (sentinelDragFormat())
            f.push_back(FORMATETC{ static_cast<CLIPFORMAT>(sentinelDragFormat()), nullptr,
                                   DVASPECT_CONTENT, -1, TYMED_HGLOBAL });
        *out = new (std::nothrow) FormatEnum(std::move(f), 0);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

    ~EditorDataObject() = default;  // public for the same reason as FormatEnum's

private:
    LONG ref_ = 1;
    std::wstring text_;  // a COPY, taken before DoDragDrop — never a view into the model
    HWND owner_ = nullptr;
};

// ---- the drop source --------------------------------------------------------
// Escape cancels, releasing the button drops, everything else keeps going. The default
// cursors are correct for a text drag and drawing our own would only lose the shell's
// copy/move/no-drop glyphs.
struct EditorDropSource final : public IDropSource {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keyState) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

    ~EditorDropSource() = default;  // public for the same reason as FormatEnum's

private:
    LONG ref_ = 1;
};

// ---- the drop target --------------------------------------------------------
struct EditorDropTarget final : public IDropTarget {
    explicit EditorDropTarget(EditorState* s) : st_(s) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD keyState, POINTL pt,
                                        DWORD* effect) override {
        if (!effect) return E_INVALIDARG;
        kind_ = classify(obj);
        local_ = (kind_ == Kind::Text) && namesThisControl(obj);
        return DragOver(keyState, pt, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL pt, DWORD* effect) override {
        if (!effect) return E_INVALIDARG;
        const DWORD allowed = *effect;
        if (kind_ == Kind::None) {
            hideDropCaret();
            *effect = DROPEFFECT_NONE;
            return S_OK;
        }
        if (kind_ == Kind::Files) {
            // A FILE is not inserted anywhere, so there is no insertion point to point at —
            // showing one would promise a paste that is not going to happen. COPY (never
            // MOVE) so the shell never deletes the file it just handed us.
            hideDropCaret();
            *effect = (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY
                      : (allowed & DROPEFFECT_LINK) ? DROPEFFECT_LINK
                                                    : DROPEFFECT_NONE;
            return S_OK;
        }
        POINT p = toClient(pt);
        edgeScroll(p);  // BEFORE the hit-test: a scroll moves what is under the cursor
        const size_t off = offsetFromPoint(st_, p.x, p.y);
        *effect = dropEffectFor(keyState, allowed, insideSourceSelection(off));
        if (*effect == DROPEFFECT_NONE)
            hideDropCaret();
        else
            showDropCaret(off);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        hideDropCaret();
        kind_ = Kind::None;
        local_ = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD keyState, POINTL pt,
                                   DWORD* effect) override {
        if (!effect) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = DROPEFFECT_NONE;
        hideDropCaret();

        // CF_HDROP FIRST, and it is not a preference. A file drag from Explorer that fell
        // through to the text branch would either do nothing or paste the file's PATH into
        // the buffer, and both are worse than the gap this whole section closes.
        const Kind k = classify(obj);
        if (k == Kind::Files) {
            if (forwardFilesToHost(obj))
                *effect = (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
            return S_OK;
        }
        if (k != Kind::Text) return S_OK;

        const bool local = namesThisControl(obj);
        local_ = local;  // Drop can arrive without a DragEnter having set it (a re-entered target)
        const POINT p = toClient(pt);
        const size_t off = offsetFromPoint(st_, p.x, p.y);
        const DWORD e = dropEffectFor(keyState, allowed, insideSourceSelection(off));
        if (e == DROPEFFECT_NONE) return S_OK;

        const bool move = (e == DROPEFFECT_MOVE);
        // A local move needs no payload at all — it moves the model's own selection, so
        // going back to the data object for a copy of text we already have would only add a
        // way to fail. Every other case does need it.
        std::wstring text;
        if (!(local && move) && !fetchText(obj, text)) return S_OK;

        SetFocus(st_->hwnd);  // dropped-in text you cannot immediately type after is half a feature
        if (!applyDrop(st_, text, off, local, move)) return S_OK;
        // Tell the SOURCE half not to delete: we already did the delete, as part of the one
        // undo step. Without this, beginTextDrag would see DROPEFFECT_MOVE come back from
        // DoDragDrop and remove the text a second time.
        if (local && move) st_->dragLocalDrop = true;
        *effect = e;
        return S_OK;
    }

    ~EditorDropTarget() = default;  // public for the same reason as FormatEnum's

private:
    enum class Kind { None, Text, Files };

    static Kind classify(IDataObject* obj) {
        if (!obj) return Kind::None;
        FORMATETC files{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (obj->QueryGetData(&files) == S_OK) return Kind::Files;
        FORMATETC text{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (obj->QueryGetData(&text) == S_OK) return Kind::Text;
        return Kind::None;
    }

    // Did this payload come out of THIS control? See sentinelDragFormat.
    bool namesThisControl(IDataObject* obj) const {
        const UINT cf = sentinelDragFormat();
        if (!obj || !cf) return false;
        FORMATETC fe{ static_cast<CLIPFORMAT>(cf), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med{};
        if (FAILED(obj->GetData(&fe, &med))) return false;
        bool same = false;
        if (med.tymed == TYMED_HGLOBAL && med.hGlobal && GlobalSize(med.hGlobal) >= sizeof(UINT64)) {
            if (const void* p = GlobalLock(med.hGlobal)) {
                UINT64 v = 0;
                std::memcpy(&v, p, sizeof(v));  // the block is not guaranteed to be aligned
                same = (reinterpret_cast<HWND>(static_cast<UINT_PTR>(v)) == st_->hwnd);
                GlobalUnlock(med.hGlobal);
            }
        }
        ReleaseStgMedium(&med);
        return same;
    }

    static bool fetchText(IDataObject* obj, std::wstring& out) {
        FORMATETC fe{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med{};
        if (FAILED(obj->GetData(&fe, &med))) return false;
        bool ok = false;
        if (med.tymed == TYMED_HGLOBAL && med.hGlobal) {
            if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(med.hGlobal))) {
                // NUL-terminated by convention, but the scan is bounded by the block anyway:
                // a source that forgets the terminator must not walk this off the heap.
                const size_t cap = GlobalSize(med.hGlobal) / sizeof(wchar_t);
                size_t n = 0;
                while (n < cap && p[n] != L'\0') ++n;
                out.assign(p, n);
                GlobalUnlock(med.hGlobal);
                ok = true;
            }
        }
        ReleaseStgMedium(&med);
        return ok;
    }

    // TRAP 2, and the reason this function exists rather than an openFile() call.
    // Registering the drop target above stopped the shell delivering WM_DROPFILES to the
    // MAIN WINDOW for anything dropped on the editor's rectangle, so this control now owns
    // file drops whether it wants them or not. It hands them straight back: MainWindow's
    // WM_DROPFILES calls requestOpenPath, which POSTS, which lands in the
    // confirmSaveIfDirty-guarded WM_APP_OPEN_PATH. That is the only route in the app that
    // cannot silently discard an unsaved buffer, and adding a second one here — however
    // carefully — would be adding a second chance to get that wrong.
    //
    // The block is COPIED byte-for-byte instead of forwarded, because ownership has to be
    // unambiguous. The host finishes with DragFinish(), which GlobalFrees what it was given,
    // while the STGMEDIUM we were handed belongs to the data object and is returned with
    // ReleaseStgMedium; passing one handle to both is a double free. Copying also means this
    // code needs to know NOTHING about the DROPFILES layout — it is the shell's own block, so
    // DragQueryFileW parses the copy exactly as it would have parsed the original.
    bool forwardFilesToHost(IDataObject* obj) const {
        HWND parent = GetParent(st_->hwnd);
        if (!parent) return false;  // standalone host (the demo, the tests): nothing to open with
        FORMATETC fe{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med{};
        if (FAILED(obj->GetData(&fe, &med))) return false;
        bool ok = false;
        if (med.tymed == TYMED_HGLOBAL && med.hGlobal) {
            const SIZE_T bytes = GlobalSize(med.hGlobal);
            HGLOBAL copy = bytes ? GlobalAlloc(GMEM_MOVEABLE, bytes) : nullptr;
            if (copy) {
                const void* src = GlobalLock(med.hGlobal);
                void* dst = GlobalLock(copy);
                if (src && dst) {
                    std::memcpy(dst, src, bytes);
                    GlobalUnlock(copy);
                    GlobalUnlock(med.hGlobal);
                    // SENT, not posted: the host copies the paths out and DragFinish()es the
                    // block before it returns, so the copy's whole lifetime is inside this
                    // call and cannot leak if the message is never dispatched. The OPEN is
                    // still deferred — requestOpenPath posts — so nothing runs on this stack.
                    SendMessageW(parent, WM_DROPFILES, reinterpret_cast<WPARAM>(copy), 0);
                    ok = true;
                } else {
                    if (dst) GlobalUnlock(copy);
                    if (src) GlobalUnlock(med.hGlobal);
                    GlobalFree(copy);
                }
            }
        }
        ReleaseStgMedium(&med);
        return ok;
    }

    POINT toClient(POINTL pt) const {
        POINT p{ pt.x, pt.y };  // DragEnter/DragOver/Drop are given SCREEN coordinates
        ScreenToClient(st_->hwnd, &p);
        return p;
    }

    bool insideSourceSelection(size_t off) const {
        if (!local_) return false;  // someone else's text can land anywhere
        const editor::Selection sel = st_->model.selection();
        // Inclusive at BOTH ends: landing exactly on either edge of the run also puts the
        // text back where it already is.
        return !sel.empty() && off >= sel.min() && off <= sel.max();
    }

    void showDropCaret(size_t off) const {
        if (st_->dropCaret && st_->dropOffset == off) return;  // nothing moved, nothing to repaint
        st_->dropCaret = true;
        st_->dropOffset = off;
        InvalidateRect(st_->hwnd, nullptr, FALSE);
    }

    void hideDropCaret() const {
        if (!st_->dropCaret) return;
        st_->dropCaret = false;
        InvalidateRect(st_->hwnd, nullptr, FALSE);
    }

    // Scroll when the cursor is within a line of an edge, so a drag can reach text that is
    // off screen. Driven by DragOver rather than by the kAutoScrollTimer: that timer's
    // handler extends the SELECTION, which is the one thing a drag must not touch, and it
    // is gated on st->selecting, which beginTextDrag clears precisely so a WM_TIMER left
    // over from the press cannot fire into a drag.
    void edgeScroll(const POINT& p) const {
        ensureFormat(st_);  // lineH / spaceW are the step sizes below
        RECT rc;
        GetClientRect(st_->hwnd, &rc);
        const int lh = (std::max)(1, static_cast<int>(st_->lineH));
        const int cw = (std::max)(1, static_cast<int>(st_->spaceW));
        const int beforeY = st_->scrollY, beforeX = st_->scrollX;
        if (p.y < lh)
            st_->scrollY -= lh;
        else if (p.y > rc.bottom - lh)
            st_->scrollY += lh;
        if (p.x < 2 * cw)
            st_->scrollX -= 4 * cw;
        else if (p.x > rc.right - 2 * cw)
            st_->scrollX += 4 * cw;
        clampScroll(st_);
        if (st_->scrollY == beforeY && st_->scrollX == beforeX) return;
        InvalidateRect(st_->hwnd, nullptr, FALSE);
        // The funnel's contract is "ANY change of scrollY/scrollX reports here, whatever
        // caused it" — including one that happened inside an OLE callback. The host's
        // consumer is one InvalidateRect of the line-number gutter, which is safe to run
        // from here; skipping it would leave the gutter reading the pre-drag line numbers.
        flushViewNotifications(st_);
    }

    LONG ref_ = 1;
    EditorState* st_ = nullptr;
    Kind kind_ = Kind::None;
    bool local_ = false;  // the payload came out of this very control
};

// Registered at WM_CREATE. Failure is NOT fatal: without an OLE apartment on the thread
// (CoInitializeEx alone is not enough — see MainWindow's OleInitialize) RegisterDragDrop
// returns CO_E_NOTINITIALIZED, and the right answer is the editor slices 2-6 shipped, not a
// half-registered control. st->dropTarget stays null and every drag path is gated on it.
void registerDropTarget(EditorState* st) {
    if (st->dropTarget) return;
    auto* target = new (std::nothrow) EditorDropTarget(st);
    if (!target) return;
    if (FAILED(RegisterDragDrop(st->hwnd, target))) {
        target->Release();  // ...and the control simply has no drag-and-drop
        return;
    }
    // Two references now: RegisterDragDrop took its own, and this one keeps the pointer
    // valid for the property below and for the Release at WM_NCDESTROY.
    st->dropTarget = target;
    SetPropW(st->hwnd, kDropTargetProp, reinterpret_cast<HANDLE>(target));
}

void revokeDropTarget(EditorState* st) {
    if (!st->dropTarget) return;
    RemovePropW(st->hwnd, kDropTargetProp);
    RevokeDragDrop(st->hwnd);  // drops OLE's reference; the target can no longer be called
    st->dropTarget->Release();
    st->dropTarget = nullptr;
}

// The SOURCE half. Called from WM_MOUSEMOVE once a press that landed inside the selection
// has travelled past the system drag threshold.
//
// Read trap 4 at the top of this section before touching anything here: DoDragDrop is a
// MODAL LOOP that pumps messages, and every line below is placed with that in mind.
void beginTextDrag(EditorState* st) {
    const editor::Selection sel = st->model.selection();
    if (sel.empty()) return;
    // COPIED OUT FIRST. model.text() is a std::wstring the model reallocates on every edit,
    // and the loop below can deliver an edit (the host posts work to itself, and a posted
    // WM_APP_OPEN_PATH replaces the whole buffer). A pointer into it would dangle; this
    // string is ours.
    const std::wstring dragged = st->model.text().substr(sel.min(), sel.max() - sel.min());

    st->dragArmed = false;
    // Selection-extension and its autoscroll are over: OLE owns the mouse from here, and a
    // WM_TIMER queued before the drag can still be delivered INSIDE it (KillTimer does not
    // purge what is already posted). onAutoScroll returns immediately while this is false.
    st->selecting = false;
    if (st->autoTimer) {
        KillTimer(st->hwnd, kAutoScrollTimer);
        st->autoTimer = 0;
    }
    if (GetCapture() == st->hwnd) ReleaseCapture();  // DoDragDrop takes its own capture
    // Stop the blink for the duration. The drop caret does not blink, and leaving the timer
    // running would repaint the window twice a second through a modal loop for nothing.
    stopBlink(st);
    st->caretOn = true;

    auto* data = new (std::nothrow) EditorDataObject(dragged, st->hwnd);
    auto* source = new (std::nothrow) EditorDropSource();
    if (!data || !source) {
        if (data) data->Release();
        if (source) source->Release();
        if (st->hasFocus) startBlink(st);
        return;
    }

    HWND hwnd = st->hwnd;  // survives st; used to re-validate below
    st->dragActive = true;
    st->dragLocalDrop = false;
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &effect);
    data->Release();
    source->Release();

    // RE-ENTRANCY, the honest version: the loop that just returned dispatched messages, and
    // one of them could in principle have destroyed this window (a posted WM_CLOSE from the
    // updater's shutdown request is the realistic route) — which would have run WM_NCDESTROY
    // and deleted `st` while this frame still held it. GWLP_USERDATA on a dead HWND reads 0,
    // so this comparison is safe on a pointer that is no longer safe to dereference.
    if (state(hwnd) != st) return;

    st->dragActive = false;
    st->dropCaret = false;
    if (st->hasFocus) startBlink(st);

    // An EXTERNAL move: the text is now in another application and this side owes the
    // delete. (A move within this control was already done by our own drop target as ONE
    // undo step, which is what dragLocalDrop records.)
    if (SUCCEEDED(hr) && effect == DROPEFFECT_MOVE && !st->dragLocalDrop) {
        const editor::Selection now = st->model.selection();
        // Delete only if the range still holds exactly the text that was handed over. If
        // the modal loop let something edit the buffer, the offsets captured before it mean
        // nothing, and deleting on their say-so would destroy unrelated text. Leaving a copy
        // behind is the recoverable failure; deleting the wrong run is not.
        if (now.min() == sel.min() && now.max() == sel.max() && now.max() <= st->model.length() &&
            st->model.text().compare(now.min(), now.max() - now.min(), dragged) == 0) {
            st->model.deleteSelection();
            afterEdit(st);  // the funnel — an unreported delete is a discardable buffer
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---- mouse selection + autoscroll (carried over, plus the X axis) -----------

void onMouseMove(EditorState* st, int mx, int my, WPARAM keys) {
    if (!st->selecting || !(keys & MK_LBUTTON)) return;
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    const bool outside = (my < 0 || my > rc.bottom || mx < 0 || mx > rc.right);
    if (outside) {
        if (!st->autoTimer) st->autoTimer = SetTimer(st->hwnd, kAutoScrollTimer, 40, nullptr);
    } else if (st->autoTimer) {
        KillTimer(st->hwnd, kAutoScrollTimer);
        st->autoTimer = 0;
    }
    const int cy = (std::min)((std::max)(my, 0), static_cast<int>(rc.bottom));
    const int cx = (std::min)((std::max)(mx, 0), static_cast<int>(rc.right));
    st->model.setCaret(offsetFromPoint(st, cx, cy), true);
    caretMoved(st, true);
}

void onAutoScroll(EditorState* st) {
    // KillTimer does not purge WM_TIMER messages already posted, so one can still arrive
    // after the drag ended -- releasing the button outside the window would otherwise scroll
    // one more step and extend the selection AFTER the mouse came up.
    if (!st->selecting) return;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(st->hwnd, &pt);
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    if (pt.y < 0)
        st->scrollY -= dpx(st, 24);
    else if (pt.y > rc.bottom)
        st->scrollY += dpx(st, 24);
    if (pt.x < 0)
        st->scrollX -= dpx(st, 32);
    else if (pt.x > rc.right)
        st->scrollX += dpx(st, 32);
    clampScroll(st);
    const int cy = (std::min)((std::max)(static_cast<int>(pt.y), 0), static_cast<int>(rc.bottom));
    const int cx = (std::min)((std::max)(static_cast<int>(pt.x), 0), static_cast<int>(rc.right));
    st->model.setCaret(offsetFromPoint(st, cx, cy), true);
    // Same rule as EM_EXSETSEL and caretMoved(…, true): this MOVED THE CARET, so a sticky
    // column from an earlier Up/Down no longer describes it. Found while auditing the
    // EM_EXSETSEL omission — this arm hand-rolls the tail of caretMoved (it must, because
    // it owns the scroll) and had inherited the same gap: drag-autoscroll out of the
    // window, release, press Down, and the caret jumps to the pre-drag column.
    st->desiredX = -1;
    InvalidateRect(st->hwnd, nullptr, FALSE);
    flushViewNotifications(st);
}

// ---- scrollbars -------------------------------------------------------------

void onScroll(EditorState* st, int bar, WPARAM wParam) {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(st->hwnd, bar, &si);
    int pos = si.nPos;
    // One "line" is a text line vertically and one character cell horizontally.
    const int unit = (bar == SB_VERT) ? static_cast<int>((std::max)(1.0f, st->lineH))
                                      : static_cast<int>((std::max)(1.0f, st->spaceW));
    switch (LOWORD(wParam)) {
        case SB_LINEUP: pos -= unit; break;    // == SB_LINELEFT
        case SB_LINEDOWN: pos += unit; break;  // == SB_LINERIGHT
        case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: break;
    }
    if (bar == SB_VERT)
        st->scrollY = pos;
    else
        st->scrollX = pos;
    clampScroll(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
    flushViewNotifications(st);  // the gutter is painted by the PARENT and repaints on this
}

// ---- window procedure -------------------------------------------------------

LRESULT CALLBACK D2DEditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* st = new (std::nothrow) EditorState();
        if (!st) return FALSE;  // fail CreateWindowEx cleanly under memory pressure
        st->hwnd = hwnd;
        const UINT d = GetDpiForWindow(hwnd);
        st->dpi = d ? d : 96;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    EditorState* st = state(hwnd);
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_CREATE:
            // The OLE drop target (slice 7). Deliberately not fatal if it fails — see
            // registerDropTarget — so returning 0 (== "creation succeeded", which is also
            // what DefWindowProcW returned for this message before) is right either way.
            registerDropTarget(st);
            return 0;
        case WM_PAINT: paint(st); return 0;
        case WM_ERASEBKGND: return 1;  // D2D clears the whole client
        case WM_SIZE: {
            // Through hwndRt, not rt: Resize is the one call left that the base
            // ID2D1RenderTarget does not have. Same object either way.
            if (st->hwndRt) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                if (st->hwndRt->Resize(D2D1::SizeU(
                        static_cast<UINT32>((std::max<LONG>)(1, rc.right - rc.left)),
                        static_cast<UINT32>((std::max<LONG>)(1, rc.bottom - rc.top)))) ==
                    D2DERR_RECREATE_TARGET)
                    discardDeviceResources(st);
            }
            // No layout invalidation: with NO_WRAP the width does not affect a single
            // line's shaping. This is the reference's rebuildLayout-on-resize, deleted.
            clampScroll(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            // GROWING THE VIEW MOVES THE VIEW. clampScroll's ceiling is
            // contentHeight - clientH, so dragging the horizontal splitter down (or
            // maximising) can drop scrollY by hundreds of pixels with nobody scrolling —
            // measured: first visible line 381 -> 331. The funnel says every change of
            // scrollY/scrollX reports, so this reports. It was harmless only because every
            // host caller of layout() happens to repaint the whole window afterwards; that
            // is a property of today's callers, not of this control, and a contract that
            // holds only while nobody adds a caller is not a contract.
            flushViewNotifications(st);
            return 0;
        }
        case WM_CHAR: onChar(st, static_cast<wchar_t>(wParam)); return 0;
        case WM_KEYDOWN:
            if (onKeyDown(st, wParam)) return 0;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            const int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const size_t off = offsetFromPoint(st, mx, my);
            const editor::Selection sel = st->model.selection();
            // A press INSIDE an existing selection is ambiguous: it starts either a drag of
            // that selection or an ordinary click that replaces it. THE CARET IS NOT MOVED
            // HERE — moving it would collapse the selection before the drag could pick it
            // up, which is the bug that makes drag-to-move impossible to add later. The
            // ambiguity is resolved by which message arrives next: WM_MOUSEMOVE past the
            // system drag threshold starts the drag, WM_LBUTTONUP without one is the click.
            // Gated on dropTarget so a control with no OLE apartment behaves exactly as it
            // did before this existed. Shift-click always extends, never drags.
            if (!shift && st->dropTarget && !sel.empty() && off >= sel.min() && off < sel.max()) {
                SetCapture(hwnd);
                st->dragArmed = true;
                st->dragArmPt = POINT{ mx, my };
                return 0;
            }
            SetCapture(hwnd);
            st->selecting = true;
            st->model.setCaret(off, shift);
            caretMoved(st, true);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            selectWordAt(st, offsetFromPoint(st, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            caretMoved(st, true);
            return 0;
        case WM_MOUSEMOVE:
            if (st->dragArmed) {
                // SM_CXDRAG/SM_CYDRAG is the system's own definition of "the user meant to
                // drag, not to click" — a few pixels, and honouring it is what keeps a
                // slightly shaky click on a selection from flinging the text somewhere.
                if (!(wParam & MK_LBUTTON)) {
                    st->dragArmed = false;  // button gone without a WM_LBUTTONUP for us
                    return 0;
                }
                const int dx = GET_X_LPARAM(lParam) - st->dragArmPt.x;
                const int dy = GET_Y_LPARAM(lParam) - st->dragArmPt.y;
                if ((dx < 0 ? -dx : dx) >= GetSystemMetrics(SM_CXDRAG) ||
                    (dy < 0 ? -dy : dy) >= GetSystemMetrics(SM_CYDRAG))
                    beginTextDrag(st);  // MODAL: does not return until the drag ends
                return 0;
            }
            onMouseMove(st, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
            return 0;
        case WM_LBUTTONUP:
            if (st->dragArmed) {
                // Pressed inside the selection and never travelled: an ordinary click, and
                // it must behave as one — collapse the selection and put the caret where
                // the press was. This is the half of the drag gesture that is exercised on
                // every stray click, so it is the half that must not misbehave.
                st->dragArmed = false;
                st->model.setCaret(offsetFromPoint(st, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)),
                                   false);
                caretMoved(st, true);
            }
            st->selecting = false;
            // Unconditional, and it has to be: an ARMED drag also holds the capture (taken
            // in WM_LBUTTONDOWN without setting `selecting`), so releasing only on the
            // selecting path would strand the mouse captured after a click on a selection.
            if (GetCapture() == hwnd) ReleaseCapture();
            if (st->autoTimer) {
                KillTimer(hwnd, kAutoScrollTimer);
                st->autoTimer = 0;
            }
            return 0;
        case WM_CAPTURECHANGED:  // capture stolen mid-drag (no WM_LBUTTONUP arrives)
            st->selecting = false;
            // DoDragDrop takes the capture itself, so this arrives at the start of every
            // text drag as well: disarming here is what stops the WM_LBUTTONUP that ends
            // the drag from also being read as a click.
            st->dragArmed = false;
            if (st->autoTimer) {
                KillTimer(hwnd, kAutoScrollTimer);
                st->autoTimer = 0;
            }
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) {
                const int unit = static_cast<int>((std::max)(1.0f, st->spaceW));
                st->scrollX -= (delta / WHEEL_DELTA) * 6 * unit;
            } else {
                const int unit = static_cast<int>((std::max)(1.0f, st->lineH));
                st->scrollY -= (delta / WHEEL_DELTA) * 3 * unit;
            }
            clampScroll(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            flushViewNotifications(st);
            return 0;
        }
        case WM_MOUSEHWHEEL: {  // tilt wheel / precision touchpad
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int unit = static_cast<int>((std::max)(1.0f, st->spaceW));
            st->scrollX += (delta / WHEEL_DELTA) * 6 * unit;
            clampScroll(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            flushViewNotifications(st);
            return 0;
        }
        case WM_VSCROLL: onScroll(st, SB_VERT, wParam); return 0;
        case WM_HSCROLL: onScroll(st, SB_HORZ, wParam); return 0;
        case WM_SETFOCUS:
            st->hasFocus = true;
            st->caretOn = true;
            startBlink(st);
            // The system caret lives exactly as long as focus does — it is a per-THREAD
            // object, so keeping one while another control owns the focus would report
            // this control's insertion point for someone else's window.
            st->sysCaret = true;
            st->sysCaretH = 0;                  // force the CreateCaret in syncSystemCaret
            st->sysCaretPt = POINT{ -1, -1 };
            syncSystemCaret(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            st->hasFocus = false;
            st->pendingHigh = 0;  // drop a half-typed surrogate rather than cross a focus change
            stopBlink(st);
            if (st->sysCaret) {
                st->sysCaret = false;
                st->sysCaretH = 0;
                DestroyCaret();  // ours: it only succeeds for the caret this thread owns
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_TIMER:
            // Both timers are dispatched from ANY message loop, including the nested
            // GetMessageW(&msg, nullptr, 0, 0) loops this app's modal dialogs pump. That
            // is fine and deliberate: blinking is pure state + InvalidateRect, and the
            // autoscroll timer is killed on WM_CAPTURECHANGED, which a modal steals.
            if (wParam == kBlinkTimer) {
                st->caretOn = !st->caretOn;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wParam == kAutoScrollTimer) {
                onAutoScroll(st);
            } else if (wParam == kDeviceRetryTimer) {
                // One-shot: kill it first, then ask for the paint that will either succeed
                // or set it again.
                KillTimer(hwnd, kDeviceRetryTimer);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            // While a drag is in flight OLE owns the cursor: GiveFeedback returns
            // DRAGDROP_S_USEDEFAULTCURSORS, and the shell sets the copy / move / no-drop
            // glyph on every DragOver. Forcing an I-beam back over the top of it would hide
            // the one piece of feedback that tells the user what the drop is going to do.
            if (st->dragActive || st->dropCaret) return TRUE;
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_IME_STARTCOMPOSITION:
            positionImeWindow(st);
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_THEMECHANGED:
            // TRAP, and it cost a stack-overflow crash to find: do NOT call
            // d2dEditorApplyTheme here. That function calls SetWindowTheme, and
            // SetWindowTheme sends WM_THEMECHANGED straight back to this window —
            // unbounded recursion, which surfaces as a 0xc000041d "exception in a user
            // callback" with no useful stack. Only the D2D brushes need refreshing here;
            // pushing the uxtheme hint down is the caller's job, exactly once.
            if (st->rt && !createBrushes(st)) discardDeviceResources(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_SETTEXT: {
            const wchar_t* s = reinterpret_cast<const wchar_t*>(lParam);
            setTextInternal(st, s ? s : L"");
            return TRUE;
        }
        case WM_GETTEXTLENGTH: return static_cast<LRESULT>(st->model.length());
        case WM_GETTEXT: {
            const size_t cap = static_cast<size_t>(wParam);
            if (cap == 0) return 0;
            const std::wstring& t = st->model.text();
            const size_t n = (std::min)(t.size(), cap - 1);
            auto* buf = reinterpret_cast<wchar_t*>(lParam);
            std::memcpy(buf, t.c_str(), n * sizeof(wchar_t));
            buf[n] = L'\0';
            return static_cast<LRESULT>(n);
        }
        // ===== the RichEdit message dialect (slice 3) ============================
        // Exactly the messages MainWindow.cpp sends to g.hEdit, and nothing else. Grepped:
        // there is no Find/Replace and no printing in this product, so EM_FINDTEXT*,
        // EM_FORMATRANGE, EM_STREAM*, EM_SETZOOM, EM_GETMODIFY, EM_LIMITTEXT,
        // EM_CHARFROMPOS, EM_GETSEL/EM_SETSEL and EM_LINEFROMCHAR are deliberately absent —
        // implementing messages nobody sends is untested code pretending to be a contract.
        // EM_SETREADONLY / EM_REPLACESEL / EM_GETTEXTRANGE / ENM_LINK belong to g.hOut, the
        // Output pane, which stays RichEdit permanently.

        // ---- text retrieval: the two messages examples/crypto.sentinel's signature
        //      ultimately rides on ---------------------------------------------------
        case EM_GETTEXTLENGTHEX: {
            // BE LENIENT, NEVER FAIL CLOSED. editorText() does s.resize(n + 1) and
            // saveFile() does s.resize(n*2 + 16) straight off this answer
            // (MainWindow.cpp:544, :803). A NEGATIVE return (an HRESULT, say, because the
            // host asks GTL_NUMCHARS without GTL_PRECISE/GTL_CLOSE — technically an
            // incomplete combination) resizes to near SIZE_MAX and throws mid-save; a ZERO
            // return for a non-empty document makes saveFile write an EMPTY file over
            // examples/crypto.sentinel, a committed SIGNED file that opens by default and
            // that Build auto-saves. So unknown flags are ignored, never rejected.
            auto* gtl = reinterpret_cast<const GETTEXTLENGTHEX*>(wParam);
            const DWORD flags = gtl ? gtl->flags : static_cast<DWORD>(GTL_NUMCHARS);
            if (flags & GTL_USECRLF) return static_cast<LRESULT>(st->model.textCrlf().size());
            return static_cast<LRESULT>(st->model.length());
        }
        case EM_GETTEXTEX: {
            auto* gt = reinterpret_cast<const GETTEXTEX*>(wParam);
            auto* buf = reinterpret_cast<wchar_t*>(lParam);
            if (!gt || !buf) return 0;
            // gt->cb is a BYTE capacity INCLUDING room for the terminator; the return value
            // is a CHARACTER count EXCLUDING it. Both call sites then do s.resize(got)
            // (MainWindow.cpp:547, :806). Reading cb as a character count would grant twice
            // the capacity in editorText()'s buffer of exactly n+1 wchar_t — a heap overrun
            // on every keystroke, since editorText() runs on every EN_CHANGE.
            const size_t cap = static_cast<size_t>(gt->cb) / sizeof(wchar_t);
            if (cap == 0) return 0;
            // THE CRLF FORK. GT_DEFAULT (0) is the internal form editorText() compares
            // against g.savedText; GT_USECRLF (1) is the on-disk form saveFile hands
            // straight to writeUtf8 with no transformation. The two are NEVER compared with
            // each other (MainWindow.cpp:536-539), which is what lets this control's
            // internal line break be '\n' where RichEdit's is a lone '\r': each
            // representation only has to be self-consistent, and in both a break is ONE
            // character, which is what keeps every host offset aligned with the
            // EM_EXSETSEL index space. GT_SELECTION/GT_RAWTEXT/GT_NOHIDDENTEXT are never
            // sent; ignore rather than fail (see the empty-file hazard above).
            const bool crlf = (gt->flags & GT_USECRLF) != 0;
            const std::wstring conv = crlf ? st->model.textCrlf() : std::wstring();
            const std::wstring& src = crlf ? conv : st->model.text();
            const size_t n = (std::min)(src.size(), cap - 1);
            std::memcpy(buf, src.c_str(), n * sizeof(wchar_t));
            buf[n] = L'\0';
            return static_cast<LRESULT>(n);
        }

        // ---- selection ----------------------------------------------------------
        case EM_EXGETSEL: {
            auto* cr = reinterpret_cast<CHARRANGE*>(lParam);
            if (!cr) return 0;
            // Normalised (cpMin <= cpMax), as RichEdit reports it. The anchor direction is
            // not observable through this message in either control, so a right-to-left
            // drag survives the host's save/restore pairs as a forward selection — fidelity,
            // not a regression, and unobservable in practice (those pairs only run after an
            // edit, which has already collapsed the selection).
            const editor::Selection sel = st->model.selection();
            cr->cpMin = static_cast<LONG>(sel.min());
            cr->cpMax = static_cast<LONG>(sel.max());
            return 0;
        }
        case EM_EXSETSEL: {
            auto* cr = reinterpret_cast<const CHARRANGE*>(lParam);
            if (!cr) return 0;
            const LONG len = static_cast<LONG>(st->model.length());
            LONG a = cr->cpMin, b = cr->cpMax;
            if (b < 0) b = len;    // cpMax == -1 means "to the end of the text" — how the
            if (a < 0) a = 0;      // deleted applyColor/applyBackColor said "select all";
            if (a > len) a = len;  // no host caller says it any more, but the dialect does.
            if (b > len) b = len;  // gotoLineCol computes lineStart + col - 1 with NO upper
            if (a > b) {           // bound, so a diagnostic whose column runs past the line
                const LONG t = a;  // end lands here — clamp, never index out of range,
                a = b;             // never assert.
                b = t;
            }
            st->model.setSelection(static_cast<size_t>(a), static_cast<size_t>(b));
            // THE STICKY COLUMN MUST DIE WITH THE SELECTION. desiredX is the line-local x
            // that Up/Down hold on to across lines, and it is only valid for the caret that
            // set it — every other caret-moving path clears it (afterEdit, caretMoved(true)).
            // Miss it here and gotoLineCol's EM_EXSETSEL + EM_SCROLLCARET leaves the OLD
            // column armed: double-click a build diagnostic, press Down, and the caret lands
            // in the column you were in before the jump. Measured against RichEdit: Ln 2 Col 3
            // there, Ln 2 Col 31 here. -1 means "recompute from the caret on the next
            // vertical move", which is exactly right for a caret someone else just placed.
            st->desiredX = -1;
            // Deliberately does NOT scroll: that is EM_SCROLLCARET's job, which is exactly
            // why gotoLineCol sends it immediately afterwards (MainWindow.cpp:1287-1288).
            // Routing it through ensureCaretVisible would have dragged the viewport across
            // the whole document on every character typed back when highlight() moved the
            // selection once per token; that storm is gone, the separation is still right.
            // It also must not set linesDirty or drop the layout cache — selection is not
            // text, and the host sends this back at us from inside our own notification.
            st->caretOn = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            flushViewNotifications(st);
            return 0;
        }
        case EM_SCROLLCARET:
            // The sole reason double-clicking a Problem, or an Output file:line:col link,
            // actually SHOWS you the line (MainWindow.cpp:1288).
            // Does NOT clear desiredX, and that is correct rather than an omission: this
            // moves the VIEW, never the caret, so a sticky column set by Up/Down is still
            // the column that caret is in. (EM_EXSETSEL, which does move it, clears it —
            // see there.)
            ensureCaretVisible(st);
            InvalidateRect(hwnd, nullptr, FALSE);
            flushViewNotifications(st);
            return 0;

        // ---- line geometry: everything the line-number gutter is built on --------
        // All four are STRICTLY READ-ONLY of the view. They are serviced from inside the
        // PARENT's WM_PAINT (MainWindow.cpp:389-417), so if one of them mutated scrollX/
        // scrollY (clampScroll does; ensureLineIndex does not) and that raised EN_VSCROLL,
        // the parent's handler would InvalidateRect the gutter during its own paint — the
        // window re-dirties itself the instant it validates, a WM_PAINT loop at 100% CPU.
        case EM_GETLINECOUNT:
            ensureLineIndex(st);
            return static_cast<LRESULT>(lineCount(st));  // always >= 1 (empty doc == 1 line)
        case EM_LINEINDEX: {
            ensureLineIndex(st);
            LONG line = static_cast<LONG>(wParam);
            // wParam is unsigned, but gotoLineCol passes `line - 1` unchecked
            // (MainWindow.cpp:1285) — a diagnostic reporting line 0 arrives here as
            // (WPARAM)-1. RichEdit's documented meaning for -1 is "the caret's line".
            if (line < 0) line = static_cast<LONG>(lineIndexForOffset(st, st->model.caret()));
            if (line < 0 || static_cast<size_t>(line) >= lineCount(st)) return -1;
            // -1 past the end is DEPENDED ON at markErrorLines :641 (`if (start < 0)
            // continue`) and :643 (`next < 0 ? -1 : next`) and gotoLineCol :1286.
            return static_cast<LRESULT>(st->lineStarts[static_cast<size_t>(line)]);
        }
        case EM_EXLINEFROMCHAR: {
            // Note the parameter swap versus EM_LINEFROMCHAR: wParam unused, lParam is the
            // offset. Must be the EXACT inverse of EM_LINEINDEX or MainWindow.cpp:1925
            // prints a negative column (it computes cpMin - lineIndex(exLineFromChar)).
            ensureLineIndex(st);
            LONG off = static_cast<LONG>(lParam);
            if (off < 0) off = static_cast<LONG>(st->model.caret());  // -1 == current line
            if (static_cast<size_t>(off) > st->model.length())
                off = static_cast<LONG>(st->model.length());
            return static_cast<LRESULT>(lineIndexForOffset(st, static_cast<size_t>(off)));
        }
        case EM_GETFIRSTVISIBLELINE: {
            // Computed by CALLING visibleRange, not by re-deriving its arithmetic: the
            // gutter's whole correctness is that this and EM_POSFROMCHAR agree about where
            // the first visible line is, and two copies of the formula is how they drift.
            size_t first = 0, last = 0;
            visibleRange(st, first, last);
            return static_cast<LRESULT>(first);
        }
        case EM_POSFROMCHAR: {
            // RichEdit's convention, which is what the host uses (MainWindow.cpp:409):
            // wParam = POINTL* (out), lParam = character index, return value ignored.
            auto* pt = reinterpret_cast<POINTL*>(wParam);
            if (!pt) return 0;
            ensureLineIndex(st);
            // ensureFormat, NOT just ensureLineIndex, and it is not belt-and-braces. The
            // pitch below is st->lineH, which only measureMetrics ever sets and only
            // ensureFormat ever calls. Before this line the arithmetic was correct purely by
            // ACCIDENT of ordering — WM_SETTEXT happens to run afterEdit -> ensureCaretVisible
            // -> xInLine -> layoutForLine -> ensureFormat before the parent's first paint can
            // ask. Any reordering that lets the gutter query a control that has not laid a
            // single line out yet gives lineH == 0 and stacks every line number at y ~ 0, on
            // top of each other, with the loop's `pt.y > edH` exit never firing so it walks
            // the whole document on every repaint. One idempotent pointer test buys that away.
            ensureFormat(st);
            LONG raw = static_cast<LONG>(lParam);
            if (raw < 0) raw = 0;
            size_t off = static_cast<size_t>(raw);
            if (off > st->model.length()) off = st->model.length();
            const size_t line = lineIndexForOffset(st, off);
            // EXACTLY drawContent's origin, so the gutter's numbers sit on the editor's
            // lines by construction — that is what "query each visible line's y from the
            // editor (no drift)" at MainWindow.cpp:390 means. Editor-CLIENT device pixels,
            // already DPI-scaled, and UNCLAMPED: the gutter loop's only early exit is
            // `if (pt.y > edH) break` (:410), so clamping y would make it walk every line
            // in the document on every repaint.
            const float lh = (std::max)(1.0f, st->lineH);
            pt->y = static_cast<LONG>(static_cast<float>(dpx(st, kInsetY) - st->scrollY) +
                                      static_cast<float>(line) * lh);
            // Short-circuit the line start — the only x the gutter ever asks for — so a
            // parent repaint does not build an IDWriteTextLayout per visible line and churn
            // the very cache trimCache exists to bound.
            const float x = (off <= st->lineStarts[line]) ? 0.0f : xInLine(st, line, off);
            pt->x = static_cast<LONG>(static_cast<float>(dpx(st, kInsetX) - st->scrollX) + x);
            return 0;
        }

        // ---- undo / redo --------------------------------------------------------
        // THE ONLY undo path in the shipping exe. runApp's accelerator table claims Ctrl+Z
        // and Ctrl+Y (MainWindow.cpp:2089-2090) and TranslateAcceleratorW runs before
        // DispatchMessageW (:2104) for hwnd AND its descendants, so those keystrokes become
        // WM_COMMAND ID_UNDO/ID_REDO on the MAIN window and never reach onKeyDown's 'Z'/'Y'
        // arms at all — that code is live only in tests/d2d_editor_demo.cpp, which has no
        // accelerator table. (Ctrl+Shift+Z DOES reach them: accelerator modifier matching
        // is exact and there is no FSHIFT entry. So Ctrl+Shift+Z redoes here where RichEdit
        // does nothing — a bonus, not a bug.) Both routes end in afterEdit, i.e. the same
        // funnel: an undo that moves the buffer AWAY from the saved point without an
        // EN_CHANGE leaves g.dirty false and the buffer is discarded with no prompt.
        // EM_CANUNDO/EM_CANREDO GATE the action, they do not merely grey the button
        // (:1958-1959, :1662-1663) — returning 0 would make undo unreachable, silently.
        case EM_CANUNDO: return st->model.canUndo() ? TRUE : FALSE;
        case EM_CANREDO: return st->model.canRedo() ? TRUE : FALSE;
        case EM_UNDO:
            if (!st->model.canUndo()) return FALSE;
            st->model.undo();
            afterEdit(st);
            return TRUE;
        case EM_REDO:
            if (!st->model.canRedo()) return FALSE;
            st->model.redo();
            afterEdit(st);
            return TRUE;

        // ---- configuration / formatting -----------------------------------------
        case EM_SETEVENTMASK: {
            const LRESULT prev = static_cast<LRESULT>(st->eventMask);
            st->eventMask = static_cast<DWORD>(lParam);
            return prev;  // see EditorState::eventMask for why the DEFAULT is all-on
        }
        case EM_SETCHARFORMAT: {
            if (wParam != SCF_ALL) {
                // SCF_SELECTION was the ~500-calls-per-keystroke colouring path
                // (MainWindow's applyColor) and the error tints (applyBackColor). Slice 4
                // replaced both — colouring is computeSpans() in drawContent, the tints are
                // d2dEditorSetErrorLines — and SLICE 8 DELETED THE CALLERS THEMSELVES. So
                // this is a no-op that reports success, and permanently: the storm's real
                // cost was undo granularity rather than pixels, and there is nothing left in
                // the IDE to send it. Nobody checks the return.
                //
                // It must NOT raise EN_CHANGE, unlike RichEdit — a deliberate divergence.
                // Formatting changes no text and g.dirty is a pure comparison over text, so
                // nothing can depend on those spurious notifications. (They also used to be
                // able to drop the host's plain-bool g.highlighting guard mid-highlight;
                // that flag went with slice 8, along with everything that set it.)
                return TRUE;
            }
            // SCF_ALL is NOT a no-op: styleEditor is the ONLY channel by which the host
            // tells the editor its typeface and size — g.hEdit
            // never receives a WM_SETFONT anywhere in the program. Ignore it and
            // Settings -> editor font silently stops working. CHARFORMAT2W is a strict
            // superset of CHARFORMATW with the identical leading layout, so reading the
            // shared prefix needs no cbSize branch. yHeight is TWIPS (the host sends
            // 11 * 20); CFM_COLOR is ignored because the base text colour already comes
            // from Theme::textPrimary in createBrushes.
            auto* cf = reinterpret_cast<const CHARFORMATW*>(lParam);
            if (!cf) return TRUE;
            std::wstring face;
            if (cf->dwMask & CFM_FACE)
                face.assign(cf->szFaceName, wcsnlen(cf->szFaceName, LF_FACESIZE));
            const float pts =
                (cf->dwMask & CFM_SIZE) ? static_cast<float>(cf->yHeight) / 20.0f : 0.0f;
            setFontInternal(st, face, pts);
            return TRUE;
        }
        case EM_SETBKGNDCOLOR: {
            // wParam == 0 means "use lParam"; non-zero means "the system window colour",
            // which for this control means "go back to whatever the Theme says".
            const COLORREF prev = st->haveBkColor ? st->bkColor : currentTheme().windowBg;
            st->haveBkColor = (wParam == 0);
            st->bkColor = static_cast<COLORREF>(lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return static_cast<LRESULT>(prev);
        }
        case EM_SETTARGETDEVICE:
            // The host's no-word-wrap idiom, (hDC = NULL, lineWidth = 1) at :251. Accepted
            // and ignored: DWRITE_WORD_WRAPPING_NO_WRAP is unconditional here and strictly
            // stronger. Not silently accepting lParam == 0 (wrap-to-window) as if it worked
            // would be better still, but the host never sends it.
            return TRUE;
        case EM_GETOLEINTERFACE:
            // No OLE interface, deliberately. This is how the migration crossed phase 18's
            // suspendUndo/resumeUndo with no host edit at all: editorDoc() got nullptr back
            // and all eight call sites became no-ops. SLICE 8 then deleted those functions —
            // they existed only to keep programmatic EM_SETCHARFORMAT off RichEdit's undo
            // stack, and EditorModel's stack holds text + selection, so a format is
            // structurally incapable of entering it — which leaves nothing in the IDE that
            // sends this. Kept as part of the dialect, and written out (with the out-param
            // nulled) rather than left to DefWindowProcW, so the contract does not rest on a
            // future caller having initialised its pointer.
            if (lParam) *reinterpret_cast<void**>(lParam) = nullptr;
            return 0;
        case WM_SETREDRAW:
            // Handled here rather than by DefWindowProcW, which implements WM_SETREDRAW by
            // toggling WS_VISIBLE — on a WS_CLIPCHILDREN parent (MainWindow.cpp:2047) that
            // lets the parent paint over the editor's rect mid-highlight, flicker RichEdit
            // does not have. There is nothing to suppress here anyway: this control only
            // ever InvalidateRects, and no WM_PAINT can be dispatched inside the host's
            // straight-line redraw-off windows because nothing pumps in them.
            //
            // So the flag earns its keep as something else entirely — it is the caller's
            // EXACT (not heuristic) marker for "programmatic bookkeeping, stay quiet", used
            // by flushViewNotifications to suppress EN_SELCHANGE. A depth COUNTER because
            // the host's windows NESTED (clearErrorMarks and markErrorLines ran inside
            // highlight()'s on the RichEdit path) and because an extra TRUE must not go
            // negative — NOT because it is safer against an unbalanced FALSE. It is
            // strictly worse there: an unbalanced FALSE leaves this at 1 forever and mutes
            // EN_SELCHANGE permanently, where a bool would be cleared by the next TRUE.
            // See EditorState::redrawOff.
            if (wParam) {
                if (st->redrawOff > 0) --st->redrawOff;
                if (st->redrawOff == 0) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                    flushViewNotifications(st);  // report the NET change the storm left
                }
            } else {
                ++st->redrawOff;
            }
            return 0;

        case WM_NCDESTROY:
            // FIRST, before anything else is torn down: while the target is registered OLE
            // holds a reference to an object that dereferences `st` on every DragOver, and
            // `st` is deleted at the bottom of this case. Revoking here rather than at
            // WM_DESTROY is deliberate — the HWND is still valid for RevokeDragDrop, and
            // WM_NCDESTROY is where the rest of this control's teardown already lives, so
            // there is one place to read rather than two.
            revokeDropTarget(st);
            stopBlink(st);
            // Normally WM_KILLFOCUS has already done this; a window destroyed while it
            // still holds the focus never gets one, and leaving a caret behind would have
            // the thread reporting an insertion point in a window that no longer exists.
            if (st->sysCaret) { st->sysCaret = false; DestroyCaret(); }
            if (st->autoTimer) KillTimer(hwnd, kAutoScrollTimer);
            discardDeviceResources(st);
            dropLayoutCache(st);
            SafeRelease(st->format);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

// ---- public API -------------------------------------------------------------

bool registerD2DEditorClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = D2DEditorProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = nullptr;  // the whole client is painted by Direct2D
    wc.lpszClassName = kD2DEditorClass;
    if (!RegisterClassExW(&wc)) return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return true;
}

std::wstring d2dEditorText(HWND edit) {
    EditorState* st = state(edit);
    return st ? st->model.text() : std::wstring();
}

std::wstring d2dEditorTextCrlf(HWND edit) {
    EditorState* st = state(edit);
    return st ? st->model.textCrlf() : std::wstring();
}

void d2dEditorSetText(HWND edit, const std::wstring& s) {
    EditorState* st = state(edit);
    if (!st) return;
    // The SAME helper WM_SETTEXT uses. These were two copies of six lines before slice 3;
    // whichever one had not been given the notification funnel would have been a silent
    // text replacement, which is exactly how a buffer is discarded with no prompt.
    setTextInternal(st, s);
}

LONG d2dEditorCaretOffset(HWND edit) {
    EditorState* st = state(edit);
    return st ? static_cast<LONG>(st->model.selection().min()) : 0;
}

void d2dEditorApplyTheme(HWND edit) {
    EditorState* st = state(edit);
    if (!st) return;
    // The scrollbars are the only non-D2D pixels in this control, so they are the only
    // thing that needs the uxtheme dark hint — and it takes BOTH halves. SetWindowTheme
    // alone leaves them stock-light (verified on screen): the undocumented
    // AllowDarkModeForWindow (uxtheme ordinal 133, the same one applyDialogDarkMode uses)
    // has to opt this HWND in first. Guarded, so missing exports just mean light bars.
    const bool dark = currentTheme().dark;
    if (HMODULE ux = GetModuleHandleW(L"uxtheme.dll")) {
        using AllowFn = BOOL(WINAPI*)(HWND, BOOL);
        if (auto allow = reinterpret_cast<AllowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133))))
            allow(edit, dark ? TRUE : FALSE);
    }
    SetWindowTheme(edit, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    if (st->rt) {
        if (!createBrushes(st)) discardDeviceResources(st);  // recreate cleanly on next paint
    }
    // No layout invalidation, and slice 4 did NOT change that — it is the point. The
    // syntax palette lives in the BRUSHES, which are recreated on the line above; the
    // layouts still carry no colour of their own, so a theme flip repaints without
    // reshaping a single line (and the offscreen/device-loss sharing survives).
    InvalidateRect(edit, nullptr, FALSE);
}

void d2dEditorSetErrorLines(HWND edit, const std::vector<int>& lines0Based) {
    EditorState* st = state(edit);
    if (!st) return;
    // Painted DECORATION, and that is the entire design. RichEdit tints error lines with
    // EM_EXSETSEL + EM_SETCHARFORMAT/CFM_BACKCOLOR, once per diagnostic, right after a
    // build — which reaches EditorModel::setSelection, clears typingRun_ and would make the
    // next character its own undo step, at exactly the moment someone starts typing again.
    // Nothing here touches the model, the selection or the undo stack: the host says which
    // lines, drawContent paints a band, and clearing is the same call with an empty vector.
    st->errorLines = lines0Based;
    std::sort(st->errorLines.begin(), st->errorLines.end());
    st->errorLines.erase(std::unique(st->errorLines.begin(), st->errorLines.end()),
                         st->errorLines.end());
    InvalidateRect(edit, nullptr, FALSE);
}

void d2dEditorUpdateDpi(HWND edit, UINT dpi) {
    EditorState* st = state(edit);
    if (!st) return;
    st->dpi = dpi ? dpi : 96;
    SafeRelease(st->format);  // the em-size, and therefore every metric, depends on DPI
    st->lineH = 0;
    st->spaceW = 0;
    ensureFormat(st);
    dropLayoutCache(st);  // every cached layout was shaped at the old size
    st->maxLineW = 0;     // and so was the horizontal high-water mark
    clampScroll(st);      // the whole document just changed height — scrollY can move
    InvalidateRect(edit, nullptr, FALSE);
    flushViewNotifications(st);  // same funnel contract as WM_SIZE / setFontInternal
}

void d2dEditorSetFont(HWND edit, const wchar_t* face, float pointSize) {
    EditorState* st = state(edit);
    if (!st) return;
    // Shares setFontInternal with EM_SETCHARFORMAT/SCF_ALL, which is how the host's
    // Settings -> editor font actually reaches this control (styleEditor, MainWindow.cpp
    // :169). Since slice 3 that is the live path; this entry point stays for the demo host
    // and for anything that wants to set the font without speaking the dialect.
    setFontInternal(st, face ? std::wstring(face) : std::wstring(), pointSize);
}

// ---- offscreen render -------------------------------------------------------

bool d2dEditorRenderToPng(HWND edit, const wchar_t* outPath) {
    EditorState* st = state(edit);
    if (!st || !outPath || !*outPath) return false;
    ID2D1Factory* d2d = d2dFactory();
    if (!d2d || !dwriteFactory()) return false;
    ensureFormat(st);
    if (!st->format) return false;  // no text format -> nothing measurable to draw

    RECT rc;
    GetClientRect(edit, &rc);
    const UINT w = static_cast<UINT>((std::max<LONG>)(1, rc.right - rc.left));
    const UINT h = static_cast<UINT>((std::max<LONG>)(1, rc.bottom - rc.top));

    // The caller owns COM init; this only asks the apartment for the factory.
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))) ||
        !wic) {
        return false;
    }

    IWICBitmap* bmp = nullptr;
    ID2D1RenderTarget* rt = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool drawn = false;
    bool ok = false;

    if (SUCCEEDED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                    &bmp)) &&
        bmp) {
        // PREMULTIPLIED, matching the bitmap's PBGRA. 96 dpi so 1 DIP == 1 pixel, exactly as
        // the hwnd target is pinned — otherwise the offscreen image would be a scaled copy of
        // the window rather than the same pixels.
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
            96.0f);
        if (SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bmp, props, &rt)) && rt) {
            // GRAYSCALE, not the window's CLEARTYPE: ClearType requires an OPAQUE target and
            // this one declares an alpha channel, so asking for it makes EndDraw fail with
            // D2DERR_WRONG_STATE and produces no pixels at all. (The Clear below is opaque,
            // so nothing in the image is actually translucent — D2D checks the target's
            // declared alpha mode, not the pixels.) The visible difference is subpixel colour
            // fringing on glyph edges, which is why the pixel test asserts ranges.
            rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

            // THE trap this function has to avoid: EditorState's brushes are bound to the
            // WINDOW's device and are illegal on this target (D2D would fail the draw). Park
            // the whole device-dependent set, build a matching one against `rt`, draw, then
            // put the original back VERBATIM — nothing is released except the temporaries, so
            // the live window still paints afterwards with exactly the resources it had.
            ID2D1RenderTarget* const savedRt = st->rt;
            const EditorState::Brushes saved = st->br;   // the WHOLE set, by value
            st->rt = rt;
            st->br = EditorState::Brushes{};  // createBrushes releases through these; cleared
                                              // so the parked set is NOT what gets freed

            if (createBrushes(st)) {
                // Same preconditions paint() establishes before its BeginDraw.
                ensureLineIndex(st);
                clampScroll(st);
                // WHY THE LAYOUT CACHE CAN BE SHARED ACROSS TWO DEVICES: the per-line
                // IDWriteTextLayouts carry no drawing effects (no SetDrawingEffect), so they
                // hold no brushes and are purely device-INDEPENDENT. That is the whole reason
                // this function can reuse the window's cache instead of building a parallel
                // one.
                // SLICE 4 KEPT THAT TRUE, and it is the reason its colouring is shaped the
                // way it is: drawContent draws the SAME layout once per coloured run under an
                // axis-aligned clip, with the brush chosen per run, so every brush belongs to
                // whichever target is drawing and nothing device-bound is ever attached to a
                // cached layout. The moment someone "simplifies" that to SetDrawingEffect,
                // every cached layout becomes device-bound, this sharing becomes a
                // cross-device draw, and both this path and the device-loss path (which
                // likewise does not invalidate layouts) break together — with this test being
                // the only thing that would notice.
                rt->BeginDraw();
                drawContent(st, rt);
                drawn = SUCCEEDED(rt->EndDraw());
            }

            releaseBrushes(st);  // the temporaries only — the parked set was cleared above
            st->rt = savedRt;
            st->br = saved;
        }
    }

    if (drawn) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(wic->CreateStream(&stream)) && stream &&
             SUCCEEDED(stream->InitializeFromFilename(outPath, GENERIC_WRITE)) &&
             SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && enc &&
             SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
             SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && frame &&
             SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h)) &&
             SUCCEEDED(frame->SetPixelFormat(&fmt)) && SUCCEEDED(frame->WriteSource(bmp, nullptr)) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    }

    SafeRelease(frame);
    SafeRelease(enc);
    SafeRelease(stream);
    SafeRelease(rt);
    SafeRelease(bmp);
    SafeRelease(wic);
    return ok;
}

}  // namespace sentinelide
