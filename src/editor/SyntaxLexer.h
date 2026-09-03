// SPDX-License-Identifier: GPL-3.0-or-later
// SyntaxLexer.h — the ONE copy of Sentinel's syntax-colouring rules (phase 46 slice 4).
//
// PROVENANCE. Unlike EditorModel.{h,cpp} beside it, this file is NOT vendored: it is
// MainWindow.cpp::highlight()'s hand lexer, lifted out verbatim in behaviour and given an
// explicit line-start STATE so it can also run one line at a time. It lives in src/editor/
// because it is pure — no Windows, no Direct2D, no COLORREF — and because both editors are
// downstream of it: the RichEdit path turns spans into EM_SETCHARFORMAT calls, and the
// Direct2D control turns the same spans into clipped DrawTextLayout calls.
//
// WHY ONE COPY. Two copies of these rules would diverge, and the divergence would be
// invisible until someone A/B'd the two editors on the same file. At slice 7 the RichEdit
// consumer is simply deleted and this unit does not change at all.
//
// THE CLASSES ARE EXACTLY WHAT highlight() PRODUCED, and no more: comment, string, number,
// keyword, and "everything else" (Default, which is never emitted — see computeSpans).
// This is a colouring lexer, not a parser: it does not know about nesting, raw strings,
// doc comments or generics, because the thing it replaces did not either. Adding a rule
// here changes BOTH editors at once, which is the point.
//
// Index space: UTF-16 code units (std::wstring / wchar_t), matching EditorModel and the
// EM_EXSETSEL offsets highlight() feeds RichEdit.
//
// Pinned by tests/syntax_lexer_test.cpp.
#pragma once

#include <string>
#include <vector>

namespace editor {

// The five colour classes. Default is the ABSENCE of a span, never a span: computeSpans
// emits only the ranges that differ from the editor's ordinary text colour, exactly as
// highlight() only issued applyColor calls for those ranges (it painted the whole document
// Theme::textPrimary first, then overrode). A consumer that needs a full cover — the
// Direct2D painter does — fills the gaps itself.
enum class SpanClass : unsigned char {
    Default = 0,
    Comment,
    String,
    Number,
    Keyword,
};

// A half-open range [begin, end) of one class. Guaranteed non-empty, ordered by begin,
// non-overlapping, and inside the text that produced it.
struct Span {
    size_t begin = 0;
    size_t end = 0;
    SpanClass cls = SpanClass::Default;
};

// The lexer state that CROSSES a chunk boundary, and the reason this header exists at all.
// Two of the rules run past the end of a line: a block comment obviously, and — less
// obviously — an unterminated string, because the string scan in highlight() never stopped
// at a newline. Both are preserved exactly; a lexer that closed strings at end of line
// would be a nicer language rule and a BEHAVIOUR CHANGE on the RichEdit path.
//
// A default-constructed LexState is "start of document", which is also what every caller
// that lexes a whole buffer passes.
struct LexState {
    enum class Mode : unsigned char { Normal = 0, BlockComment, StringLit };
    Mode mode = Mode::Normal;
    wchar_t quote = 0;  // the quote character that will close it, when mode == StringLit

    bool operator==(const LexState& o) const { return mode == o.mode && quote == o.quote; }
    bool operator!=(const LexState& o) const { return !(*this == o); }
};

// Classify [text, text+len) starting in `state`; `out` is CLEARED and filled with the
// non-Default spans, and `state` is left holding the state that applies to whatever
// follows the chunk.
//
// THE CHUNKING CONTRACT, which is what makes per-line lexing equal whole-document lexing:
// a chunk may be the whole buffer, or ONE LINE WITHOUT its line break. Those two give the
// same spans for the same line, because every rule that could straddle the boundary
// treats the missing '\n' the same way the whole-buffer scan does —
//   * a line comment ends at '\r', '\n' OR the end of the chunk, and always leaves Normal;
//   * "*/" is never matched across the boundary, and cannot be: in the whole buffer the
//     '\n' sits between the '*' and the '/', so that scan does not match it either;
//   * a backslash at the very end of a line escapes the '\n' in the whole buffer and
//     nothing here, and both leave the next line in StringLit with no escape pending —
//     the escaped character is the line break, which belongs to neither line.
// Get that wrong and the two editors disagree about a file, which is precisely the class
// of bug that having one lexer is supposed to make impossible.
void computeSpans(const wchar_t* text, size_t len, LexState& state, std::vector<Span>& out);

// Convenience for whole-buffer callers (the RichEdit path and the tests).
std::vector<Span> computeSpans(const std::wstring& text, LexState& state);

// The same scan with the spans thrown away — for building a per-line start-state index
// without allocating a span vector per line. Shares ONE implementation with computeSpans;
// it is not a second copy of the rules.
void advanceState(const wchar_t* text, size_t len, LexState& state);

// The keyword set behind SpanClass::Keyword. Exposed because it is the one part of the
// rules a test can assert on directly.
bool isKeyword(const wchar_t* word, size_t len);

}  // namespace editor
