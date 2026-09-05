// SPDX-License-Identifier: GPL-3.0-or-later
// FindBar.h — Find / Replace for the Direct2D editor (phase 49). A MODELESS band that
// sits between the tab strip and the editor.
//
// WHY MODELESS, AND NOT A DIALOG. Every other secondary window in this program is a modal
// (Settings, Project Settings, Signing, Save Changes, …) and each one runs its own
// `GetMessageW(&msg, nullptr, 0, 0)` pump after `EnableWindow(owner, FALSE)`. Find cannot
// be one of those, for four independent reasons — any one of them would be enough:
//
//   1. A modal DISABLES the main window, and the main window is where the editor is. The
//      whole feature is "select the match and scroll it into view"; a disabled editor
//      cannot take the click that follows, cannot be typed into, and cannot be scrolled by
//      the user while the box is up. Find-as-you-type into a disabled editor is a
//      contradiction, not a limitation.
//   2. `uiIsBusy()` (MainWindow.cpp) reports true whenever the main window is disabled, and
//      three deferral paths consult it — an externally delivered open path, an update
//      offer, and a manual check's result — each of which re-arms a 4-SECOND timer and
//      tries again. A modal find box is open for as long as the user is working, so those
//      three would spin for minutes. The deferral exists to step around a dialog that is
//      about to close; a find bar is not that.
//   3. runApp's accelerator table is installed in runApp's OWN loop. A modal's private
//      pump never calls TranslateAcceleratorW, so inside a modal find dialog Ctrl+S,
//      Ctrl+Z, F5 and Ctrl+F itself would all be dead. Modeless keeps the one loop, so
//      every existing chord keeps working while find is open — including Ctrl+F and Ctrl+H
//      while the focus is in the find field.
//   4. A modal's null-filter pump dispatches SENT messages, and this bar's Replace All
//      raises EN_CHANGE synchronously. Keeping the bar modeless keeps every edit on the
//      main window's own stack, which is where afterEdit's ordering contract already holds.
//
// The cost of modeless is that Tab/Enter/Escape are not free: runApp calls
// IsDialogMessageW for this window (and only while the focus is inside it), which is what
// turns Enter into WM_COMMAND(IDOK) and Escape into WM_COMMAND(IDCANCEL) here.
//
// WHAT IT DOES NOT HOLD: the buffer. The bar owns the needle, the options and which match
// is current; every match comes from d2dEditorFindAll over the control's own std::wstring,
// and every mutation goes through d2dEditorReplaceRanges, which runs the control's full
// notification funnel. There is no second copy of the text and no second index space.
#pragma once

#include <windows.h>

#include <string>

namespace sentinelide {

inline constexpr const wchar_t* kFindBarClass = L"SentinelFindBar";

// The bar's child-control ids. Public ONLY so tests/d2d_dialect_test.cpp can reach the real
// fields and buttons with GetDlgItem and drive them the way a user does — typing WM_CHAR
// into the actual EDIT, BM_CLICK on the actual BUTTON — rather than through a test-only
// entry point that could pass while the shipping path is broken.
//
// THEY START AT 101, AND THAT IS NOT COSMETIC — it cost a bug to find and the bug was
// silent. The first draft numbered them from 1, which collides with IDOK (1) and IDCANCEL
// (2): the bar's WM_COMMAND handler switches on the id, and an EDIT control sends
// EN_UPDATE, EN_SETFOCUS and EN_KILLFOCUS with the SAME id and a different notification
// code. So every EN_UPDATE from the find field — one per character typed — fell into the
// `case IDOK:` arm and ran Find Next. The effect was find-as-you-type walking forward
// through the file by one match per keystroke: no crash, no warning, and plausible enough
// on a small file to be missed. Windows reserves 1-11 for the standard dialog buttons;
// anything a window shares a WM_COMMAND switch with IsDialogMessageW must stay out of them.
enum FindBarCtrl : int {
    kFindField = 101, kFindReplaceField, kFindCount, kFindPrev, kFindNext,
    kFindMatchCase, kFindWholeWord, kFindClose, kFindReplaceOne, kFindReplaceAll
};

// The bar SENDS this to its parent as WM_COMMAND(kFindBarLayoutCmd, 0), lParam = the bar's
// HWND, whenever its height changes — it opened, it closed, or it switched between Find
// and Replace. The parent's whole job is layout() + InvalidateRect: the bar never sizes or
// positions itself, because the editor rectangle it takes its band out of is the parent's
// arithmetic and having two owners of that is how a pane ends up overlapping another.
// 1090 sits in the gap between MainWindow's MenuId block (1001…) and its tier block (1100).
inline constexpr UINT kFindBarLayoutCmd = 1090;

// Creates the bar HIDDEN. `editor` is the D2D editor control it searches.
HWND createFindBar(HWND parent, HWND editor, HINSTANCE hInst, UINT dpi, HFONT ui);

// Open in Find mode (replaceMode false) or Replace mode, seeding the field from the
// editor's selection when that selection is a single short line, and focus the field with
// its text selected. Already open: it just switches mode and re-focuses.
void showFindBar(HWND bar, bool replaceMode);

// Close and put the focus back in the editor. The selection is deliberately left exactly
// where the last match put it — Escape ends the search, it does not undo the navigation.
void hideFindBar(HWND bar);

bool findBarVisible(HWND bar);
int findBarHeight(HWND bar);  // client pixels at the current DPI; 0 when hidden

// F3 / Shift+F3. dir is +1 (next) or -1 (previous). Opens the bar if it is closed, so the
// chord works before Ctrl+F has ever been pressed.
void findBarStep(HWND bar, int dir);

// The editor's buffer changed (a keystroke, an undo, a file load) — every offset the bar
// holds is stale, so re-run the search and re-count. Cheap and idempotent.
void findBarBufferChanged(HWND bar);

void findBarUpdateDpi(HWND bar, UINT dpi, HFONT ui);
void findBarApplyTheme(HWND bar);

// What the bar currently believes, with no screen involved. This exists for
// tests/d2d_dialect_test.cpp: the match count and the current index are the two things the
// feature is *about*, and reading them off painted pixels would be a test of the font.
struct FindBarStatus {
    int count = 0;        // matches in the buffer
    int current = -1;     // index of the selected match, -1 when there is none
    bool visible = false;
    bool replaceMode = false;
};
FindBarStatus findBarStatus(HWND bar);

}  // namespace sentinelide
