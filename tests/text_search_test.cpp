// SPDX-License-Identifier: GPL-3.0-or-later
// text_search_test — Find and Replace, everything about them that needs no window.
//
// Build+run:  cmake --build build --target text_search_test && build\text_search_test.exe
// or via ctest:  ctest --test-dir build -R text_search
//
// TWO UNITS IN ONE BINARY, deliberately: src/editor/TextSearch.{h,cpp} (which ranges match)
// and EditorModel::replaceRanges (what happens to the buffer when they are replaced). They
// are split across two files because one is pure matching and the other owns the undo
// stack, but they are ONE feature, and coverage of a feature that has to be assembled from
// two test binaries is coverage nobody reads.
//
// THE THREE ASSERTIONS THIS FILE EXISTS FOR, in the order they would hurt:
//
//   1. REPLACE ALL IS ONE UNDO STEP (case 7). A loop of insertText per match compiles,
//      runs, and looks right — and then Ctrl+Z after replacing 300 occurrences undoes the
//      300th and leaves 299 done. The check is exact: after Replace All, ONE undo must
//      restore the original buffer CHARACTER FOR CHARACTER, and the undo stack must have
//      grown by exactly one.
//   2. A MATCH NEVER SPLITS A SURROGATE PAIR (case 5). A needle that is half of a pair —
//      which a find field really can hold, by paste or IME — must find NOTHING rather than
//      select or replace one half of a codepoint. The buffer here carries real astral
//      characters, not a description of some.
//   3. THE CASE-FOLDING BOUNDARY IS WHERE THE HEADER SAYS IT IS (case 3). Ordinal ASCII +
//      Latin-1, and nothing else. This is a test of a DOCUMENTED limitation: Σ not matching
//      σ is the specified behaviour, and if someone widens the fold they have to come here
//      and say so rather than discover it from a bug report.
//
// No Windows, no Direct2D, no file on disk: it runs anywhere and in milliseconds.
#include "editor/EditorModel.h"
#include "editor/TextSearch.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace editor;

static int gPass = 0, gFail = 0;
static void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

static std::vector<Range> find(const std::wstring& hay, const std::wstring& needle,
                               bool matchCase = false, bool wholeWord = false) {
    SearchOptions o;
    o.matchCase = matchCase;
    o.wholeWord = wholeWord;
    std::vector<Range> out;
    findAll(hay, needle, o, out);
    return out;
}

// Render the matches as "a-b,c-d" so a failure prints what was actually found.
static std::string spell(const std::vector<Range>& m) {
    std::string s;
    for (size_t i = 0; i < m.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(m[i].start) + "-" + std::to_string(m[i].end);
    }
    return s.empty() ? std::string("(none)") : s;
}

int main() {
    printf("text_search_test - Find/Replace, everything testable without a window\n");

    // ---- 1 ------------------------------------------------------------------
    printf("\n1. the basics: every occurrence, in order, non-overlapping\n");
    {
        const std::wstring hay = L"let x = 1\nlet y = 2\nlet z = 3\n";
        auto m = find(hay, L"let");
        printf("     'let' in 3 lines -> %s\n", spell(m).c_str());
        check(m.size() == 3, "three matches");
        check(m[0].start == 0 && m[0].end == 3, "first is [0,3)");
        check(m[1].start == 10 && m[2].start == 20, "and they are in ascending order");

        // Non-overlapping: "aa" in "aaa" is ONE match, not two. Overlapping matches would
        // make Replace All's result depend on the order the replacements were applied.
        auto ov = find(L"aaa", L"aa");
        printf("     'aa' in 'aaa' -> %s\n", spell(ov).c_str());
        check(ov.size() == 1 && ov[0].start == 0, "non-overlapping: one match at 0");

        check(find(hay, L"").empty(), "an EMPTY needle matches nothing, not everywhere");
        check(find(L"ab", L"abc").empty(), "a needle longer than the buffer matches nothing");
        check(find(L"", L"x").empty(), "an empty buffer matches nothing");
        check(find(hay, hay).size() == 1, "the whole buffer matches itself, once");
    }

    // ---- 2 ------------------------------------------------------------------
    printf("\n2. case sensitivity, and that the default is INSENSITIVE\n");
    {
        const std::wstring hay = L"Let LET let lEt";
        check(find(hay, L"let").size() == 4, "insensitive (the default) finds all four");
        check(find(hay, L"let", true).size() == 1, "sensitive finds only the exact one");
        check(find(hay, L"LET", true).size() == 1, "...and only the exact one the other way");
        check(find(hay, L"let", true)[0].start == 8, "the case-sensitive hit is the right one");
    }

    // ---- 3 ------------------------------------------------------------------
    // The DOCUMENTED boundary of the fold. See the long note in TextSearch.h: this is
    // ordinal ASCII + Latin-1, chosen over a Unicode table or a Windows call, and every
    // non-fold below is specified behaviour rather than an oversight.
    printf("\n3. case folding is ordinal, ASCII + Latin-1, and stops there\n");
    {
        check(foldCase(L'A') == L'a' && foldCase(L'Z') == L'z', "ASCII A-Z folds");
        check(foldCase(L'a') == L'a' && foldCase(L'5') == L'5', "lowercase and digits are left alone");
        check(foldCase(0x00C9) == 0x00E9, "Latin-1: E-acute folds to e-acute");
        check(foldCase(0x00DE) == 0x00FE, "Latin-1: THORN folds to thorn (the top of the block)");
        // U+00D7 is the multiplication sign, a maths operator sitting inside the letter
        // block. Folding it would make x match / (U+00F7).
        check(foldCase(0x00D7) == 0x00D7, "U+00D7 (multiplication sign) does NOT fold");
        check(foldCase(0x0141) == 0x0141, "Latin Extended-A (L-stroke) does NOT fold - documented");
        check(foldCase(0x03A3) == 0x03A3, "Greek SIGMA does NOT fold - documented");
        check(foldCase(0x00FF) == 0x00FF, "y-diaeresis does NOT fold (its capital is U+0178) - documented");

        // ...and the same rules seen through a search, which is where a user meets them.
        // Spelled with \uXXXX escapes rather than literal characters ON PURPOSE: a test
        // whose meaning depends on this file's byte encoding surviving every tool that
        // ever touches it is a test that will one day pass for the wrong reason. The
        // decomposed case below is the one that could not be written any other way.

        check(find(L"CAF\u00C9 caf\u00E9", L"caf\u00E9").size() == 2,
              "CAFE-acute finds cafe-acute: the case the lexer comment says really occurs");
        check(find(L"\u03A3\u03C3", L"\u03C3").size() == 1,
              "SIGMA does not find sigma - the stated limit, not a bug");
        check(find(L"\u00DF", L"SS").empty(), "sharp-s does not match SS (no full case folding)");
        // Normalisation is a different question again and nothing here does any.
        check(find(L"e\u0301", L"\u00E9").empty(),
              "decomposed e+acute does not match precomposed e-acute (no normalisation)");
    }

    // ---- 4 ------------------------------------------------------------------
    printf("\n4. whole word uses the model's OWN word predicate\n");
    {
        const std::wstring hay = L"let letter outlet let_x let;";
        auto all = find(hay, L"let");
        auto ww = find(hay, L"let", false, true);
        printf("     'let' anywhere -> %zu, whole-word -> %s\n", all.size(), spell(ww).c_str());
        check(all.size() == 5, "five occurrences ignoring boundaries");
        check(ww.size() == 2, "two of them are whole words");
        check(ww[0].start == 0, "...the bare 'let' at the start");
        check(ww[1].start == 24, "...and the one before the semicolon");
        // '_' is a word character in EditorModel::isWordChar, so let_x is NOT a match.
        check(find(L"let_x", L"let", false, true).empty(), "'_' is a word character, so let_x is not a hit");
        check(find(L"a\u00E9b", L"a", false, true).empty(),
              "a non-ASCII letter is a word character too (iswalnum), so this is not a hit");
    }

    // ---- 5 ------------------------------------------------------------------
    // THE CORRUPTION CASE. A find field really can end up holding one half of a surrogate
    // pair — paste half an emoji, or an IME hands one over — and a match that starts or
    // ends mid-pair would select, and then REPLACE, half of a codepoint.
    printf("\n5. surrogate pairs are never split by a match\n");
    {
        // U+1F600 GRINNING FACE = D83D DE00; U+1F601 = D83D DE01. Two astral characters
        // sharing a high surrogate, which is the shape that makes a naive matcher wrong.
        const std::wstring hay = L"a\U0001F600b\U0001F601c";
        check(hay.size() == 7, "the fixture really is 7 UTF-16 code units");

        auto lone_low = find(hay, std::wstring(1, (wchar_t)0xDE00));
        printf("     lone LOW surrogate needle -> %s\n", spell(lone_low).c_str());
        check(lone_low.empty(), "a lone low surrogate finds NOTHING (it would split a pair)");

        auto lone_high = find(hay, std::wstring(1, (wchar_t)0xD83D));
        printf("     lone HIGH surrogate needle -> %s\n", spell(lone_high).c_str());
        check(lone_high.empty(), "a lone high surrogate finds nothing either");

        // The whole pair is fine, and lands on the pair's own boundaries.
        auto whole = find(hay, L"\U0001F600");
        check(whole.size() == 1 && whole[0].start == 1 && whole[0].end == 3,
              "the complete astral character matches, on its own boundaries");
        // ...and so does a needle that merely CONTAINS one.
        auto around = find(hay, L"a\U0001F600");
        check(around.size() == 1 && around[0].start == 0 && around[0].end == 3,
              "a needle containing an astral character matches whole");

        // And a replace over that match leaves a well-formed buffer.
        EditorModel m;
        m.setText(hay);
        check(m.replaceRanges(whole, L"X"), "replacing the astral match reports a change");
        check(m.text() == L"aXb\U0001F601c", "the OTHER pair survived intact");
    }

    // ---- 6 ------------------------------------------------------------------
    printf("\n6. next/previous, and the wrap at both ends\n");
    {
        auto m = find(L"a.a.a", L"a");   // matches at 0, 2, 4
        check(m.size() == 3, "three matches at 0, 2, 4");
        check(matchAtOrAfter(m, 0) == 0, "from 0 -> the first");
        check(matchAtOrAfter(m, 1) == 1, "from just past it -> the second");
        check(matchAtOrAfter(m, 4) == 2, "from the last match's start -> the last");
        check(matchAtOrAfter(m, 5) == 0, "PAST THE END WRAPS to the first");
        check(matchBefore(m, 5) == 2, "backwards from the end -> the last");
        check(matchBefore(m, 2) == 0, "backwards from the middle -> the one before it");
        check(matchBefore(m, 0) == 2, "BEFORE THE FIRST WRAPS to the last");
    }

    // ---- 7 ------------------------------------------------------------------
    // THE ONE THIS FILE EXISTS FOR. See the header.
    printf("\n7. REPLACE ALL is ONE undo step and restores the buffer exactly\n");
    {
        std::wstring src;
        for (int i = 0; i < 300; ++i) src += L"let v" + std::to_wstring(i) + L" = 1\n";
        EditorModel m;
        m.setText(src);
        check(!m.canUndo(), "a freshly set buffer has no undo history");

        auto matches = find(m.text(), L"let");
        printf("     %zu occurrences of 'let' in %zu chars\n", matches.size(), m.text().size());
        check(matches.size() == 300, "300 occurrences to replace");

        check(m.replaceRanges(matches, L"const"), "Replace All reports that it changed the buffer");
        check(m.text().find(L"let ") == std::wstring::npos, "not one 'let ' is left");
        check(find(m.text(), L"const").size() == 300, "and 300 'const' are there instead");

        // ONE step. Not 300, not 2.
        m.undo();
        check(m.text() == src, "ONE undo restores the original buffer CHARACTER FOR CHARACTER");
        check(!m.canUndo(), "...and the undo stack is empty again - it really was one step");
        m.redo();
        check(find(m.text(), L"const").size() == 300, "redo puts all 300 back, also in one step");
    }

    // ---- 8 ------------------------------------------------------------------
    printf("\n8. replaceRanges refuses what it cannot do safely\n");
    {
        EditorModel m;
        m.setText(L"abcdef");
        const std::wstring before = m.text();

        check(!m.replaceRanges({}, L"X"), "an empty range list changes nothing");
        check(!m.replaceRanges({ { 3, 1 } }, L"X"), "a reversed range is refused");
        check(!m.replaceRanges({ { 0, 2 }, { 1, 3 } }, L"X"), "OVERLAPPING ranges are refused");
        check(!m.replaceRanges({ { 4, 99 } }, L"X"), "an out-of-bounds range is refused");
        check(!m.replaceRanges({ { 2, 4 }, { 0, 1 } }, L"X"), "a DESCENDING list is refused");
        check(m.text() == before, "...and after all five refusals the buffer is untouched");
        check(!m.canUndo(), "...with no undo step pushed by any of them");

        // A rewrite whose result is identical is not an edit. Pushing an undo step for it
        // would leave the user pressing Ctrl+Z twice to get past an invisible no-op.
        check(!m.replaceRanges(find(m.text(), L"abc"), L"abc"),
              "replacing text with itself reports NO change");
        check(!m.canUndo(), "...and pushes no undo step");
        // The same thing arrived at through a case-INSENSITIVE match, which is the case a
        // guess based on comparing the two strings would miss.
        m.setText(L"ABC");
        check(!m.replaceRanges(find(m.text(), L"abc"), L"ABC"),
              "...even when the needle only folded to the replacement");
    }

    // ---- 9 ------------------------------------------------------------------
    printf("\n9. replacement shapes: shorter, longer, empty, and the selection left behind\n");
    {
        EditorModel m;
        m.setText(L"one two one two one");
        check(m.replaceRanges(find(m.text(), L"one"), L"1"), "shrinking replacement");
        check(m.text() == L"1 two 1 two 1", "every occurrence shrank, offsets stayed aligned");
        // The LAST replacement is selected, so the caret ends where the work ended.
        check(m.selection().min() == 12 && m.selection().max() == 13,
              "the last replacement is left selected");

        m.setText(L"a-a-a");
        check(m.replaceRanges(find(m.text(), L"a"), L"LONGER"), "growing replacement");
        check(m.text() == L"LONGER-LONGER-LONGER", "growing works too");

        m.setText(L"keep DROP keep DROP");
        check(m.replaceRanges(find(m.text(), L"DROP"), L""), "replacing with NOTHING is a delete-all");
        check(m.text() == L"keep  keep ", "...and deletes exactly the matches");
        check(m.selection().empty(), "an empty replacement leaves a bare caret, not a selection");
    }

    // ---- 10 -----------------------------------------------------------------
    printf("\n10. line breaks are '\\n' here, so a needle can cross one\n");
    {
        // The model normalises to '\n', so a search for "\r\n" must find nothing and a
        // search for "\n" must find the breaks. Getting this backwards would make Replace
        // All on a line ending silently do nothing on every file in the project.
        EditorModel m;
        m.setText(L"a\r\nb\r\nc");
        check(m.text() == L"a\nb\nc", "CRLF normalised on the way in");
        check(find(m.text(), L"\r\n").empty(), "there are no CRLF pairs left to find");
        check(find(m.text(), L"\n").size() == 2, "two '\\n' breaks are findable");
        check(find(m.text(), L"a\nb").size() == 1, "a needle can span a line break");
        check(m.replaceRanges(find(m.text(), L"\n"), L" "), "replacing the breaks works");
        check(m.text() == L"a b c", "...and joins the lines");
        check(m.textCrlf() == L"a b c", "the CRLF form of a one-line buffer has no CR");
    }

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
