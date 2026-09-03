// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/EditorModel.h"

#include <cwctype>

namespace editor {

namespace {

constexpr size_t kMaxUndo = 200;

bool isHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool isLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// In SQLTerminal these came from sqlcore::text and were ASCII-only ([A-Za-z0-9_]).
// Sentinel's own tokenizer treats an identifier as `iswalnum(c) || c == '_'`
// (MainWindow.cpp::highlight), which is Unicode-aware — and Sentinel sources really do
// carry non-ASCII identifiers (tests/diag_xcheck.cpp has a "cafe"-with-accent case). Word
// navigation that disagreed with the highlighter about where a word ends would be a
// visible papercut, so the Sentinel definition wins here. This is the ONLY behavioural
// change made while vendoring.
bool isWordChar(wchar_t c) { return iswalnum(c) != 0 || c == L'_'; }
bool isWhitespace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' ||
           c == L'\r' || c == L'\v' || c == L'\f';
}

}  // namespace

std::wstring EditorModel::textCrlf() const {
    std::wstring out;
    out.reserve(text_.size() + text_.size() / 16);   // most lines gain one byte
    for (wchar_t c : text_) {
        if (c == L'\n') out += L"\r";
        out += c;
    }
    return out;
}

std::wstring normalizeNewlines(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'\r') {
            out.push_back(L'\n');
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;  // collapse \r\n
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// ---- offset helpers --------------------------------------------------------

size_t EditorModel::snap(size_t off) const {
    off = clamp(off);
    // Don't allow the caret to sit between the halves of a surrogate pair; floor
    // it to the start of the pair.
    if (off > 0 && off < text_.size() && isHighSurrogate(text_[off - 1]) &&
        isLowSurrogate(text_[off]))
        --off;
    return off;
}

size_t EditorModel::stepCodepoint(size_t off, int dir) const {
    off = clamp(off);
    if (dir > 0) {
        if (off >= text_.size()) return text_.size();
        size_t n = off + 1;
        if (isHighSurrogate(text_[off]) && n < text_.size() && isLowSurrogate(text_[n])) ++n;
        return n;
    }
    if (off == 0) return 0;
    size_t n = off - 1;
    if (isLowSurrogate(text_[n]) && n > 0 && isHighSurrogate(text_[n - 1])) --n;
    return n;
}

size_t EditorModel::lineStart(size_t off) const {
    size_t i = clamp(off);
    while (i > 0 && text_[i - 1] != L'\n') --i;
    return i;
}

size_t EditorModel::lineEnd(size_t off) const {
    size_t i = clamp(off);
    while (i < text_.size() && text_[i] != L'\n') ++i;
    return i;
}

size_t EditorModel::wordLeft(size_t off) const {
    size_t i = clamp(off);
    while (i > 0 && isWhitespace(text_[i - 1])) --i;  // skip whitespace
    if (i == 0) return 0;
    const bool word = isWordChar(text_[i - 1]);  // class of the run we'll cross
    while (i > 0 && !isWhitespace(text_[i - 1]) && isWordChar(text_[i - 1]) == word) --i;
    return i;
}

size_t EditorModel::wordRight(size_t off) const {
    size_t i = clamp(off);
    if (i < text_.size() && !isWhitespace(text_[i])) {
        const bool word = isWordChar(text_[i]);
        while (i < text_.size() && !isWhitespace(text_[i]) && isWordChar(text_[i]) == word) ++i;
    }
    while (i < text_.size() && isWhitespace(text_[i])) ++i;  // skip trailing whitespace
    return i;
}

// ---- text / selection ------------------------------------------------------

void EditorModel::setText(const std::wstring& s) {
    text_ = normalizeNewlines(s);
    sel_.anchor = sel_.caret = text_.size();
    undo_.clear();
    redo_.clear();
    typingRun_ = false;
}

void EditorModel::setCaret(size_t off, bool extend) {
    off = snap(off);
    sel_.caret = off;
    if (!extend) sel_.anchor = off;
    typingRun_ = false;
}

void EditorModel::setSelection(size_t anchor, size_t caret) {
    sel_.anchor = snap(anchor);
    sel_.caret = snap(caret);
    typingRun_ = false;
}

void EditorModel::selectAll() {
    sel_.anchor = 0;
    sel_.caret = text_.size();
    typingRun_ = false;
}

// ---- undo bookkeeping ------------------------------------------------------

void EditorModel::recordPreEdit(bool extendRun) {
    if (extendRun && typingRun_) return;  // coalesce into the open typing step
    undo_.push_back({text_, sel_});
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void EditorModel::undo() {
    if (undo_.empty()) return;
    redo_.push_back({text_, sel_});
    const Snapshot s = undo_.back();
    undo_.pop_back();
    text_ = s.text;
    sel_ = s.sel;
    typingRun_ = false;
}

void EditorModel::redo() {
    if (redo_.empty()) return;
    undo_.push_back({text_, sel_});
    const Snapshot s = redo_.back();
    redo_.pop_back();
    text_ = s.text;
    sel_ = s.sel;
    typingRun_ = false;
}

// ---- editing ---------------------------------------------------------------

void EditorModel::insertText(const std::wstring& s) {
    if (s.empty()) return;
    const bool run = (s.size() == 1 && s[0] != L'\n' && sel_.empty());
    recordPreEdit(run);
    if (!sel_.empty()) {
        const size_t a = sel_.min(), b = sel_.max();
        text_.erase(a, b - a);
        sel_.anchor = sel_.caret = a;
    }
    text_.insert(sel_.caret, s);
    sel_.caret += s.size();
    sel_.anchor = sel_.caret;
    typingRun_ = run;
}

void EditorModel::moveSelectionTo(size_t dest) {
    if (sel_.empty()) return;
    const size_t a = sel_.min(), b = sel_.max();
    dest = snap(clamp(dest));
    // Dropping onto the selection itself, including exactly on either edge: nothing to do,
    // and no undo step to push. This is checked BEFORE recordPreEdit on purpose — an
    // "undo" that restored an identical buffer would be an invisible no-op the user has to
    // press Ctrl+Z twice to get past.
    if (dest >= a && dest <= b) return;
    // ONE snapshot for the whole move. extendRun is false: a move is never part of a
    // typing run, and letting it coalesce into one would fold an unrelated keystroke into
    // the same undo step.
    recordPreEdit(false);
    const std::wstring moved = text_.substr(a, b - a);
    text_.erase(a, b - a);
    // The erase shifted everything after `b` down by the length of the run, so a
    // destination beyond it has to come down with it. (Destinations before `a` are
    // untouched by the erase; there is no third case, since dest inside [a, b] returned
    // above.)
    const size_t at = (dest > b) ? dest - (b - a) : dest;
    text_.insert(at, moved);
    sel_.anchor = at;
    sel_.caret = at + moved.size();
    typingRun_ = false;
}

void EditorModel::deleteSelection() {
    if (sel_.empty()) return;
    recordPreEdit(false);
    const size_t a = sel_.min(), b = sel_.max();
    text_.erase(a, b - a);
    sel_.anchor = sel_.caret = a;
    typingRun_ = false;
}

void EditorModel::backspace() {
    if (!sel_.empty()) {
        deleteSelection();
        return;
    }
    if (sel_.caret == 0) return;
    recordPreEdit(false);
    const size_t from = stepCodepoint(sel_.caret, -1);
    text_.erase(from, sel_.caret - from);
    sel_.anchor = sel_.caret = from;
    typingRun_ = false;
}

void EditorModel::deleteForward() {
    if (!sel_.empty()) {
        deleteSelection();
        return;
    }
    if (sel_.caret >= text_.size()) return;
    recordPreEdit(false);
    const size_t to = stepCodepoint(sel_.caret, +1);
    text_.erase(sel_.caret, to - sel_.caret);
    typingRun_ = false;
}

void EditorModel::deleteWordLeft() {
    if (sel_.empty()) setSelection(sel_.caret, wordLeft(sel_.caret));
    deleteSelection();
}

void EditorModel::deleteWordRight() {
    if (sel_.empty()) setSelection(sel_.caret, wordRight(sel_.caret));
    deleteSelection();
}

// ---- logical navigation ----------------------------------------------------

void EditorModel::moveLeft(bool extend, bool byWord) {
    if (!extend && !byWord && !sel_.empty()) {
        const size_t m = sel_.min();
        sel_.anchor = sel_.caret = m;
        typingRun_ = false;
        return;
    }
    const size_t c = byWord ? wordLeft(sel_.caret) : stepCodepoint(sel_.caret, -1);
    setCaret(c, extend);
}

void EditorModel::moveRight(bool extend, bool byWord) {
    if (!extend && !byWord && !sel_.empty()) {
        const size_t m = sel_.max();
        sel_.anchor = sel_.caret = m;
        typingRun_ = false;
        return;
    }
    const size_t c = byWord ? wordRight(sel_.caret) : stepCodepoint(sel_.caret, +1);
    setCaret(c, extend);
}

void EditorModel::moveLineHome(bool extend) { setCaret(lineStart(sel_.caret), extend); }
void EditorModel::moveLineEnd(bool extend) { setCaret(lineEnd(sel_.caret), extend); }
void EditorModel::moveDocStart(bool extend) { setCaret(0, extend); }
void EditorModel::moveDocEnd(bool extend) { setCaret(text_.size(), extend); }

}  // namespace editor
