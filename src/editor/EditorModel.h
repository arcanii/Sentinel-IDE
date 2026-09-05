// SPDX-License-Identifier: GPL-3.0-or-later
// EditorModel.h — the pure, platform-free text model behind the Direct2D editor.
//
// VENDORED from G:\SQLTerminal-Win32\src\editor\EditorModel.{h,cpp} (same author, same
// GPL-3.0 licence, same lineage as this repo's Win32 shell — see THIRD-PARTY-NOTICES.txt).
// Copied essentially verbatim; the comments below still describe SQLTerminal's editor in
// places, which is deliberate — divergence should be visible, not silently smoothed over.
//
// TWO changes were made while vendoring, both marked at their site in the .cpp:
//   1. the word-class predicate is Sentinel's `iswalnum(c) || c == '_'` rather than
//      SQLTerminal's ASCII-only [A-Za-z0-9_], so word navigation agrees with what
//      MainWindow.cpp::highlight treats as an identifier;
//   2. textCrlf() was added — the on-disk CRLF form, which EM_GETTEXTEX/GT_USECRLF will be
//      implemented on so saveFile keeps working unchanged against either editor.
//
// Pinned by tests/editor_model_test.cpp (39 assertions). The one that matters most is the
// golden case: examples/crypto.sentinel is a COMMITTED SIGNED file that opens by default
// and that Build auto-saves, so this model must round-trip it byte-for-byte or saving
// would invalidate its .sig without anyone pressing Ctrl+S.
//
// This is the unit-tested core of the Direct2D/DirectWrite editor that replaces
// RichEdit (see src/ui/SqlEditorControl.*). It owns the text buffer, the caret
// and selection, all edit operations, *logical* (wrap-independent) navigation,
// and an undo/redo stack. It deliberately knows nothing about Windows, Direct2D,
// or visual layout, so the bulk of the editor's behavior can be verified by
// ctest (EditorCoreTests) without a screen.
//
// Index space: UTF-16 code units (std::wstring / wchar_t), matching SqlCore and
// the highlighter so the editor's offsets line up byte-for-byte with
// SqlSyntaxHighlighter / SqlStatementSplitter. The buffer always uses single
// '\n' line breaks (set/paste normalize '\r\n' and lone '\r' to '\n'), which is
// exactly what editorText() handed to SqlCore before.
//
// What lives elsewhere (the view): up/down/page-up/down navigation is *visual*
// (it depends on word wrap) and is computed by the control via DirectWrite
// hit-testing, then applied here through setCaret(offset, extend).
#pragma once

#include <string>
#include <vector>

namespace editor {

// A selection is a half-open range [min, max) in UTF-16 code units. `anchor` is
// where the selection started; `caret` is the moving end where the caret blinks.
// An empty selection (anchor == caret) is a bare caret.
struct Selection {
    size_t anchor = 0;
    size_t caret = 0;

    size_t min() const { return anchor < caret ? anchor : caret; }
    size_t max() const { return anchor < caret ? caret : anchor; }
    bool empty() const { return anchor == caret; }

    bool operator==(const Selection& o) const {
        return anchor == o.anchor && caret == o.caret;
    }
    bool operator!=(const Selection& o) const { return !(*this == o); }
};

// A half-open [start, end) span in the same UTF-16 index space as Selection. Added in
// phase 49 for Find/Replace: src/editor/TextSearch.h reports matches as Ranges and
// replaceRanges consumes them, so a match never changes index space on its way to the
// buffer. Not a Selection, deliberately — a Selection has a moving end and a still one,
// and a match has neither.
struct Range {
    size_t start = 0;
    size_t end = 0;
};

// Convert '\r\n' and lone '\r' to '\n'. Free function so it can be unit-tested
// directly and reused by the control's paste path.
std::wstring normalizeNewlines(const std::wstring& s);

class EditorModel {
public:
    EditorModel() = default;

    // ---- text access -------------------------------------------------------
    // Always '\n' line breaks; safe to hand straight to the highlighter / DWrite.
    const std::wstring& text() const { return text_; }
    size_t length() const { return text_.size(); }

    // The buffer with '\n' expanded back to '\r\n' — the on-disk form, and what
    // EM_GETTEXTEX with GT_USECRLF must return, so MainWindow's saveFile keeps
    // working untouched against either editor. Added here, not in the control, because it
    // is pure text and therefore testable without a window: a Sentinel source file has to
    // round-trip setText -> textCrlf byte-for-byte, or saving would rewrite the file with
    // LF and invalidate a committed .sig (examples/crypto.sentinel is signed, opens by
    // default, and Build auto-saves it).
    std::wstring textCrlf() const;

    // Hard reset (initial text, history/snippet/schema insert, clear-after-run).
    // Normalizes newlines, places the caret at the end, and clears undo history
    // (matches RichEdit's WM_SETTEXT, which discards the undo buffer).
    void setText(const std::wstring& s);

    // ---- selection / caret -------------------------------------------------
    Selection selection() const { return sel_; }
    size_t caret() const { return sel_.caret; }
    // Move the caret to `off` (clamped, snapped off surrogate boundaries). When
    // `extend` is true the anchor is kept (Shift+navigation); otherwise the
    // selection collapses to the caret.
    void setCaret(size_t off, bool extend);
    void setSelection(size_t anchor, size_t caret);
    void selectAll();

    // ---- editing -----------------------------------------------------------
    void insertText(const std::wstring& s);  // replaces the selection if any

    // Drag-and-drop MOVE: lift the current selection out and re-insert it at `dest` (an
    // offset in the text as it is NOW, before the lift), as ONE undo step.
    //
    // WHY IT LIVES HERE and is not two calls in the control. A move spelled
    // deleteSelection() + insertText() is TWO recordPreEdit(false) calls, i.e. two
    // snapshots, i.e. Ctrl+Z leaves the text with the selection deleted and nothing
    // re-inserted — a half-move on screen, which is worse than either end state. Undo
    // granularity is the undo stack's business, and the undo stack is in here.
    //
    // A `dest` INSIDE [min, max] is a no-op, deliberately and not defensively: dropping a
    // selection onto itself must change nothing and must not even push an undo step.
    // (The bounds are inclusive at both ends — landing exactly on either edge also puts
    // the text back where it started.)
    //
    // Afterwards the moved run is SELECTED, so it can be dragged straight on again, which
    // is what every other editor does and what makes a mis-drop cheap to correct.
    void moveSelectionTo(size_t dest);

    // REPLACE ALL, and it is one call for one reason: ONE undo step (phase 49).
    //
    // `ranges` are half-open spans of the text AS IT IS NOW, ascending and
    // non-overlapping — exactly what TextSearch::findAll produces. Each is replaced by
    // `repl`, which may be empty (replace-with-nothing is a delete-all).
    //
    // WHY IT LIVES HERE, and it is the same argument moveSelectionTo makes one function
    // up. Spelled as a loop of setSelection + insertText in the control it would be ONE
    // recordPreEdit PER MATCH — Ctrl+Z after replacing 300 occurrences would undo the
    // 300th and leave 299 done, and the user would have to hold Ctrl+Z to get their file
    // back, watching it un-replace one match at a time. Undo granularity is the undo
    // stack's business and the undo stack is in here, so the whole rewrite takes exactly
    // one snapshot and one Ctrl+Z restores the buffer verbatim.
    //
    // A rewrite that produces an IDENTICAL buffer (replacing "x" with "x") pushes NO undo
    // step and mutates nothing — same reasoning as moveSelectionTo's drop-onto-itself
    // case: an undo step that restores an identical buffer is an invisible no-op the user
    // has to press Ctrl+Z twice to get past. It is checked against the built result, not
    // guessed from the arguments, so a needle that folds to the replacement under a
    // case-insensitive search is caught too.
    //
    // Malformed input (descending, overlapping, or out of bounds) changes NOTHING and
    // pushes no undo step. That is not defensive garnish: these offsets come from a search
    // over a buffer that a notification handler could in principle have changed underneath
    // us, and a partial rewrite of a user's file is the one outcome worth refusing outright.
    //
    // Afterwards the LAST replacement is selected, so the caret ends where the work ended
    // and the view can be scrolled to something the user can see was done.
    // Returns true iff the buffer actually changed — which is exactly the condition
    // under which the control must run its notification funnel. Returning it, rather
    // than having the caller guess, is what stops a no-op Replace All raising an
    // EN_CHANGE that would set g.dirty on a buffer identical to the one on disk.
    bool replaceRanges(const std::vector<Range>& ranges, const std::wstring& repl);

    void backspace();                        // delete selection, else codepoint left
    void deleteForward();                    // delete selection, else codepoint right
    void deleteSelection();                  // no-op when empty
    void deleteWordLeft();                   // Ctrl+Backspace
    void deleteWordRight();                  // Ctrl+Delete

    // ---- logical navigation (wrap-independent; what the WNDPROC calls) ------
    void moveLeft(bool extend, bool byWord);
    void moveRight(bool extend, bool byWord);
    void moveLineHome(bool extend);
    void moveLineEnd(bool extend);
    void moveDocStart(bool extend);
    void moveDocEnd(bool extend);

    // ---- offset helpers (const; reused by the view) ------------------------
    size_t stepCodepoint(size_t off, int dir) const;  // ±1 codepoint, pair-aware
    size_t lineStart(size_t off) const;               // after the previous '\n'
    size_t lineEnd(size_t off) const;                 // at the next '\n' or end
    size_t wordLeft(size_t off) const;                // Ctrl+Left target
    size_t wordRight(size_t off) const;               // Ctrl+Right target
    size_t docStart() const { return 0; }
    size_t docEnd() const { return text_.size(); }

    // ---- undo / redo -------------------------------------------------------
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo();  // restores text AND selection
    void redo();

private:
    struct Snapshot {
        std::wstring text;
        Selection sel;
    };

    size_t clamp(size_t off) const { return off > text_.size() ? text_.size() : off; }
    size_t snap(size_t off) const;  // never land between a surrogate pair

    // Push the current state as an undo step before mutating. When `extendRun`
    // is true and a typing run is already open, the edit coalesces into the
    // current step (no new snapshot). Always clears the redo stack.
    void recordPreEdit(bool extendRun);

    std::wstring text_;
    Selection sel_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool typingRun_ = false;  // a run of coalescing single-char inserts is open
};

}  // namespace editor
