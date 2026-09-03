// SPDX-License-Identifier: GPL-3.0-or-later
// D2DEditor.h — the no-wrap Direct2D/DirectWrite code-editor control (slice 2 of the
// RichEdit replacement; see docs/HANDOVER.md phase 46).
//
// Window class: "SentinelD2DEditor". It owns an editor::EditorModel (the vendored,
// unit-tested text model) and renders it with ONE IDWriteTextLayout PER LINE, laid out
// lazily and only for lines that are actually on screen.
//
// SCOPE. Slice 2 built the renderer + input surface; SLICE 3 added the RichEdit message
// dialect (the ~20 EM_* messages MainWindow.cpp actually sends) and the synchronous
// EN_CHANGE / EN_SELCHANGE / EN_VSCROLL / EN_HSCROLL funnel, and linked the control into
// the Sentinel-IDE exe behind ONE CreateWindowExW in createControls, DEFAULT OFF
// ([editor] d2d in settings.ini, or --d2d-editor / --richedit for one run).
// SLICE 4 added syntax colouring and the error-line tints, both PAINTED: the rules come
// from src/editor/SyntaxLexer.h (the same unit the RichEdit path uses, so the two editors
// cannot disagree about a file), and drawContent draws each coloured run by clipping the
// ONE per-line layout — never IDWriteTextLayout::SetDrawingEffect, which would make the
// shared, device-loss-surviving layout cache device-bound. EM_SETCHARFORMAT with
// SCF_SELECTION stays a permanent no-op: the host does not send it to this control,
// because its cost is undo granularity rather than pixels.
// tests/d2d_editor_demo.cpp remains a standalone host for driving it without the IDE.
//
// WHY NOT THE REFERENCE'S SHAPE. SQLTerminal's SqlEditorControl builds ONE
// IDWriteTextLayout over the whole document and word-wraps it. For a query box that is
// fine; for a code editor it is wrong twice over — code must not wrap, and a
// document-wide layout is O(document) work per keystroke on a file that can be tens of
// thousands of lines. Everything that is not layout (device lifecycle, clipboard, caret
// blink, IME, surrogate buffering, mouse autoscroll) is carried over from there almost
// verbatim; the layout/geometry half is per-line arithmetic written fresh. With no wrap a
// visual line IS a logical line, so that half is simpler than the reference, not harder.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sentinelide {

// The window class name. Register once per process before CreateWindowExW.
inline constexpr const wchar_t* kD2DEditorClass = L"SentinelD2DEditor";

// Idempotent: returns true if the class is registered (including "already registered").
bool registerD2DEditorClass(HINSTANCE hInst);

// ---- host-facing accessors -------------------------------------------------
// Deliberately prefixed: MainWindow.cpp already has its own editorText() over RichEdit,
// and slice 3 has to be able to compile both paths side by side.

// The buffer with '\n' line breaks — what the highlighter/compiler sees.
std::wstring d2dEditorText(HWND edit);
// The buffer with '\r\n' — the on-disk form (EditorModel::textCrlf). saveFile's
// byte-identity requirement rides on this; see the golden test, editor_model_test case 10.
std::wstring d2dEditorTextCrlf(HWND edit);

void d2dEditorSetText(HWND edit, const std::wstring& s);
LONG d2dEditorCaretOffset(HWND edit);

// Rebuild theme-dependent device resources (call on a light/dark flip).
void d2dEditorApplyTheme(HWND edit);
// Rebuild DPI-dependent metrics (call from WM_DPICHANGED). The app is
// per-monitor-v2 aware, so this is a real code path, not a formality.
void d2dEditorUpdateDpi(HWND edit, UINT dpi);
// Change the typeface/size (slice 3 wires this to Settings::editorFont).
void d2dEditorSetFont(HWND edit, const wchar_t* face, float pointSize);

// Which lines carry a build diagnostic, 0-BASED (the EM_LINEINDEX index space, i.e. the
// host's `d.line - 1`). The control paints a band behind them in
// blendColor(windowBg, diagError, 24) — the same colour RichEdit's CFM_BACKCOLOR tint
// uses. An EMPTY vector clears them; that is clearErrorMarks' whole implementation here.
//
// WHY THIS EXISTS AT ALL rather than the host just sending EM_SETCHARFORMAT. The RichEdit
// tint is applied by SELECTING each line and setting a character background, once per
// diagnostic, immediately after a build. On this control that would reach
// EditorModel::setSelection and clear typingRun_, so the next character typed would start
// its own undo step — a user-visible regression bought for a background colour. Decoration
// the control paints itself has no such coupling, and the host keeps its g.errorMarks
// bookkeeping either way. (Duplicates are tolerated and sorted out here; two diagnostics on
// one line is normal.)
void d2dEditorSetErrorLines(HWND edit, const std::vector<int>& lines0Based);

// ---- offscreen render (how this control's output is TESTED) -----------------
// Renders the control's current view into a WIC bitmap the same size as its client
// area and writes it out as a PNG. Returns false on any failure (no D2D/WIC, bad
// path, encode error) and leaves no partial file worth trusting.
//
// WHY THIS EXISTS. The control's rendering had no automated coverage: a blank editor
// would compile, run, open a window and pass every other test in the repo. This path
// needs no window, no desktop and no human, so tests/d2d_render_test.cpp can assert on
// real pixels in CI — the regression net for "the editor renders a blank window", and
// what makes slices 4-7 (syntax colouring) verifiable at all.
//
// For simply LOOKING at the control, scripts\capture.ps1 works on the live window — but
// only while that window is FOREGROUND. (An earlier version of this comment claimed
// PrintWindow could not capture Direct2D at all. It was wrong and is retracted:
// re-measured, a magenta-cleared D2D window captures as magenta and the demo captures with
// its real text. The actual condition is compositing — a background or minimised D2D
// window has nothing for PrintWindow to copy and captures blank every time, and a
// background process cannot foreground it, so an automated run cannot capture this control
// at all. Use the test above, which needs no window.)
//
// The caller owns COM init (CoInitializeEx) — this creates the WIC factory but will not
// initialize an apartment behind the caller's back.
//
// It does NOT disturb the live window: the device-dependent set (target + brushes) is
// swapped for the length of one draw and restored verbatim, so the window still paints
// afterwards with exactly the resources it had.
bool d2dEditorRenderToPng(HWND edit, const wchar_t* outPath);

}  // namespace sentinelide
