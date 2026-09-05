// SPDX-License-Identifier: GPL-3.0-or-later
// TextSearch.h — the matcher behind Find / Replace (phase 49).
//
// Pure text logic over the editor's UTF-16 buffer: no Windows, no Direct2D, no
// EditorModel state. That is what lets tests/text_search_test.cpp assert every rule in
// here without a window, and it is why the file sits beside EditorModel.cpp rather than
// in src/host/win32/.
//
// INDEX SPACE is EditorModel's: UTF-16 code units, '\n' line breaks. A Range handed back
// from here goes straight into EditorModel::setSelection / replaceRanges with no
// conversion, which is the whole point — a second index space would be a second place for
// a surrogate pair to be cut in half.
//
// SURROGATE PAIRS ARE NEVER SPLIT. A match is rejected outright if either end would land
// between a high and a low surrogate, which is exactly EditorModel::snap's condition. That
// can only happen when the NEEDLE itself begins with a lone low surrogate or ends with a
// lone high one (search boxes can hold one: paste half an emoji, or type it through an
// IME), and the alternative — snapping the range outwards — would select or replace a
// codepoint the user never asked for. Refusing is the only answer that cannot corrupt the
// buffer: findAll simply reports no match there.
//
// CASE-INSENSITIVITY IS ORDINAL AND PARTIAL, and the boundary is stated rather than
// implied, because this is the rule most likely to surprise. foldCase() maps
//   * ASCII      'A'-'Z'  -> 'a'-'z'
//   * Latin-1    U+00C0-U+00DE (minus U+00D7 ×, not a letter) -> U+00E0-U+00FE
// and NOTHING ELSE. So CAFÉ finds café — the case the lexer's own comment says Sentinel
// sources really do hit ("Sentinel sources really do carry non-ASCII identifiers",
// EditorModel.cpp) — while every one of these is a NON-match, deliberately and knowingly:
//   * Latin Extended-A and beyond: Ł/ł, Ş/ş, Ā/ā, and all of Greek and Cyrillic (Σ/σ,
//     Д/д). They fold under Unicode's rules; they do not fold here.
//   * ß vs SS, ﬁ vs FI and every other 1:many full case folding.
//   * ÿ (U+00FF) vs Ÿ (U+0178) — the one Latin-1 letter whose uppercase escapes the block.
//   * The Turkish dotless-i pair, which needs a locale to get right and would be wrong
//     for everyone else if hard-coded.
//   * Anything requiring NORMALISATION: a precomposed é (U+00E9) does not match a
//     decomposed e + U+0301, in either case mode. Nothing here normalises.
// Why not more: the alternatives are a Unicode case-folding table (a data set this project
// has no other use for, and one that must then be versioned) or a Windows call —
// CompareStringOrdinal(..., TRUE) or LCMapStringEx — which would drag the OS into a file
// whose entire value is that it has no OS in it. Ordinal-and-stated beats
// almost-right-and-unstated: a user who searches for "Σ" and finds only "Σ" has a rule
// they can predict.
//
// WHOLE WORD uses EXACTLY EditorModel's word predicate (iswalnum(c) || c == '_'), not an
// ASCII one. Word-boundary search that disagreed with Ctrl+Left/Ctrl+Right about where a
// word ends would be the same papercut the vendoring note in EditorModel.cpp already
// argued away once.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "editor/EditorModel.h"  // editor::Range — matches are in the model's index space

namespace editor {

struct SearchOptions {
    bool matchCase = false;   // default OFF: case-insensitive is what a code editor opens with
    bool wholeWord = false;
};

// Ordinal case fold — ASCII + Latin-1 only. See the header note; exposed so the test can
// pin the boundary directly rather than inferring it from search results.
wchar_t foldCase(wchar_t c);

// Every non-overlapping occurrence of `needle` in `hay`, left to right, in ascending
// order. An empty needle matches NOTHING (not "everywhere"): a find bar is empty most of
// the time it is open, and n+1 zero-width matches is neither a useful count nor a
// selectable range.
//
// Non-overlapping means "aa" finds ONE match in "aaa", at 0. Overlapping matches would
// make Replace All's output depend on the order the replacements were applied.
void findAll(const std::wstring& hay, const std::wstring& needle,
             const SearchOptions& opt, std::vector<Range>& out);

// Index into `matches` of the first match starting at or after `from`, WRAPPING to 0 when
// there is none — that wrap IS the find-next-wraps-around behaviour, and it is here rather
// than at the call site so next and previous cannot wrap by different rules. An EMPTY
// `matches` is the caller's to check first; both of these answer 0 for it, which is a
// valid index of nothing.
size_t matchAtOrAfter(const std::vector<Range>& matches, size_t from);

// Index into `matches` of the last match ENDING at or before `from`, wrapping to the last
// match. The mirror of the above for Shift+F3; `from` is the selection's min, so pressing
// previous on a selected match steps off it rather than finding it again.
size_t matchBefore(const std::vector<Range>& matches, size_t from);

}  // namespace editor
