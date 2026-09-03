// SPDX-License-Identifier: GPL-3.0-or-later
// syntax_lexer_test — the colouring rules, on their own (phase 46 slice 4).
//
// Build+run:  cmake --build build --target syntax_lexer_test && build\syntax_lexer_test.exe
// or via ctest:  ctest --test-dir build -R syntax_lexer
//
// WHY THIS TEST EXISTS. These rules shipped for 45 phases as an inline loop inside
// MainWindow.cpp::highlight() with no coverage of any kind — the only way to check them was
// to open a file and look. Slice 4 lifted them into src/editor/SyntaxLexer.{h,cpp} and gave
// them a SECOND consumer (the Direct2D control paints the same spans), which raises the
// stakes twice over: a change here changes what every user sees in both editors, and the
// two must never disagree about the same file.
//
// THE LOAD-BEARING CASES ARE THE ONES THAT CROSS A LINE. The Direct2D control colours only
// the lines it can see, one at a time, from a cached start state; the RichEdit path lexes
// the whole buffer in one go. Case 12 asserts those two produce the SAME classification for
// every character, on inputs chosen to straddle every boundary the rules have: a block
// comment spanning lines, an unterminated block comment, an unterminated string (which,
// per the rules as they have always been, swallows the rest of the file), and a backslash
// as a line's last character — where the escaped character is the line break itself and
// belongs to neither line.
//
// Pure C++: no Windows, no Direct2D, no COLORREF. It runs in milliseconds and needs no
// desktop, which is the point of the lexer being a separate unit at all.
#include "editor/SyntaxLexer.h"

#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

using namespace editor;

static int gPass = 0, gFail = 0;
static void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

// Lex a whole buffer from the start of a document, the way highlight() does.
static std::vector<Span> spansOf(const std::wstring& s) {
    LexState st;
    return computeSpans(s, st);
}

// The class covering `off`, or Default where no span does — i.e. exactly what a consumer
// paints at that character. Comparing THIS rather than the span vectors is what makes the
// per-line/whole-buffer equivalence in case 12 meaningful: the two chop the ranges up
// differently on purpose (a per-line lexer cannot emit a span across a line break), but
// every character must still come out the same colour.
static SpanClass classAt(const std::vector<Span>& v, size_t off) {
    for (const Span& s : v) {
        if (off >= s.begin && off < s.end) return s.cls;
    }
    return SpanClass::Default;
}

static const char* className(SpanClass c) {
    switch (c) {
        case SpanClass::Comment: return "comment";
        case SpanClass::String:  return "string";
        case SpanClass::Number:  return "number";
        case SpanClass::Keyword: return "keyword";
        case SpanClass::Default: break;
    }
    return "default";
}

// The invariant every caller relies on and no caller checks: ordered, non-overlapping,
// non-empty, inside the text, and never Default (which is the ABSENCE of a span).
static bool spansWellFormed(const std::vector<Span>& v, size_t len) {
    size_t prevEnd = 0;
    for (const Span& s : v) {
        if (s.begin >= s.end) return false;
        if (s.end > len) return false;
        if (s.begin < prevEnd) return false;
        if (s.cls == SpanClass::Default) return false;
        prevEnd = s.end;
    }
    return true;
}

// Classify a buffer the way the Direct2D control does: one line at a time, without its
// line break, carrying the lexer state across. Returns the class of every character;
// line breaks themselves are marked Default and skipped by the comparison, since no
// per-line consumer ever paints one.
static std::vector<SpanClass> perLineClasses(const std::wstring& s) {
    std::vector<SpanClass> out(s.size(), SpanClass::Default);
    std::vector<Span> spans;
    LexState st;
    size_t lineStart = 0;
    while (lineStart <= s.size()) {
        size_t lineEnd = lineStart;
        while (lineEnd < s.size() && s[lineEnd] != L'\n') ++lineEnd;
        computeSpans(s.c_str() + lineStart, lineEnd - lineStart, st, spans);
        if (!spansWellFormed(spans, lineEnd - lineStart)) return std::vector<SpanClass>();
        for (const Span& sp : spans) {
            for (size_t k = sp.begin; k < sp.end; ++k) out[lineStart + k] = sp.cls;
        }
        if (lineEnd >= s.size()) break;
        lineStart = lineEnd + 1;  // step over the '\n'
    }
    return out;
}

// Does per-line lexing agree with whole-buffer lexing, character for character?
static bool sameEitherWay(const std::wstring& s, const char* label) {
    const std::vector<Span> whole = spansOf(s);
    const std::vector<SpanClass> byLine = perLineClasses(s);
    if (byLine.size() != s.size()) {
        printf("     %s: per-line pass produced malformed spans\n", label);
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\n') continue;
        const SpanClass w = classAt(whole, i);
        if (w != byLine[i]) {
            printf("     %s: offset %zu ('%lc') whole=%s per-line=%s\n", label, i,
                   s[i] == L'\r' ? L' ' : s[i], className(w), className(byLine[i]));
            return false;
        }
    }
    return true;
}

int main() {
    printf("SyntaxLexer\n\n");

    printf("1. line comments run to the line break and no further\n");
    {
        const std::wstring s = L"let a = 1; // tail comment\nlet b = 2;";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, s.find(L"//")) == SpanClass::Comment, "the // itself is comment");
        check(classAt(v, s.find(L"tail")) == SpanClass::Comment, "text after // is comment");
        check(classAt(v, s.find(L'\n')) == SpanClass::Default, "the newline is NOT in the span");
        check(classAt(v, s.find(L"let b")) == SpanClass::Keyword, "the next line lexes normally");
        check(classAt(v, s.find(L"1;")) == SpanClass::Number, "code before it is untouched");
        // '\r' as well as '\n': the RichEdit buffer's line break is a lone CR, so one rule
        // has to end a line comment in both representations or highlight() would paint the
        // rest of the file green on that path.
        const std::wstring cr = L"a // c\rlet b = 2;";
        const std::vector<Span> vcr = spansOf(cr);
        check(classAt(vcr, cr.find(L'\r')) == SpanClass::Default, "a lone CR ends it too");
        check(classAt(vcr, cr.find(L"let")) == SpanClass::Keyword, "and code resumes after it");
    }

    printf("\n2. block comments, including across lines\n");
    {
        const std::wstring s = L"let a = /* one\ntwo\nthree */ 7;";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, s.find(L"/*")) == SpanClass::Comment, "opens at /*");
        check(classAt(v, s.find(L"two")) == SpanClass::Comment, "a whole middle line is comment");
        check(classAt(v, s.find(L"*/") + 1) == SpanClass::Comment, "the closing / is included");
        check(classAt(v, s.find(L"7;")) == SpanClass::Number, "and it really did close");
        check(classAt(v, s.find(L"let")) == SpanClass::Keyword, "before it is untouched");

        const std::wstring tight = L"/**/x";
        check(spansOf(tight).size() == 1 && spansOf(tight)[0].end == 4, "/**/ is exactly 4 wide");
        const std::wstring nested = L"/* /* still one comment */ x";
        const std::vector<Span> nv = spansOf(nested);
        check(nv.size() == 1 && nv[0].end == nested.find(L"*/") + 2,
              "block comments do NOT nest (they never did)");
        check(classAt(nv, nested.size() - 1) == SpanClass::Default, "so the tail is code");
    }

    printf("\n3. an unterminated block comment swallows the rest of the buffer\n");
    {
        const std::wstring s = L"let a = 1;\n/* no end in sight\nlet b = 2;\n";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, s.find(L"/*")) == SpanClass::Comment, "opens");
        check(classAt(v, s.size() - 1) == SpanClass::Comment, "and still open at the last char");
        check(classAt(v, s.find(L"let b")) == SpanClass::Comment,
              "so a keyword inside it is NOT a keyword");
        LexState st;
        std::vector<Span> tmp;
        computeSpans(s.c_str(), s.size(), st, tmp);
        check(st.mode == LexState::Mode::BlockComment, "and the end state says so");

        // The other half of the same fact: a chunk that STARTS inside a block comment.
        LexState open;
        open.mode = LexState::Mode::BlockComment;
        std::vector<Span> cont;
        const std::wstring line = L"still comment */ let x = 1;";
        computeSpans(line.c_str(), line.size(), open, cont);
        check(open.mode == LexState::Mode::Normal, "a resumed block comment can close");
        check(classAt(cont, 0) == SpanClass::Comment, "its head is comment");
        check(classAt(cont, line.find(L"let")) == SpanClass::Keyword, "its tail is code again");
    }

    printf("\n4. strings, and escaped quotes inside them\n");
    {
        const std::wstring s = L"let m = \"say \\\"hi\\\" now\"; let n = 3;";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, s.find(L'"')) == SpanClass::String, "opens at the quote");
        check(classAt(v, s.find(L"hi")) == SpanClass::String, "an escaped quote does not close it");
        check(classAt(v, s.find(L"now")) == SpanClass::String, "still inside after the escapes");
        check(classAt(v, s.find(L"; let")) == SpanClass::Default, "and it closed at the real end");
        check(classAt(v, s.find(L"3;")) == SpanClass::Number, "code after it lexes normally");
        // A backslash escapes whatever follows, including another backslash — so a string
        // ending in \\ IS closed by the next quote.
        const std::wstring bs = L"\"a\\\\\" + 5";
        const std::vector<Span> bv = spansOf(bs);
        check(classAt(bv, bs.find(L'5')) == SpanClass::Number, "\\\\ does not eat the closing quote");
    }

    printf("\n5. an unterminated string swallows the rest of the buffer (as it always has)\n");
    {
        // Deliberately pinned rather than "fixed". Ending a string at the line break would
        // be a nicer language rule AND a change to what the RichEdit editor has always
        // painted, which is not what a refactor gets to do.
        const std::wstring s = L"let a = \"open\nlet b = 2;\n";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, s.find(L"open")) == SpanClass::String, "opens");
        check(classAt(v, s.find(L"let b")) == SpanClass::String, "and crosses the line break");
        check(classAt(v, s.size() - 1) == SpanClass::String, "to the very end");
        LexState st;
        std::vector<Span> tmp;
        computeSpans(s.c_str(), s.size(), st, tmp);
        check(st.mode == LexState::Mode::StringLit && st.quote == L'"',
              "the end state carries the mode AND which quote closes it");

        // The quote character matters: a ' inside a "..." must not close it, and vice
        // versa. That is why LexState carries the quote instead of a bare flag.
        LexState open;
        open.mode = LexState::Mode::StringLit;
        open.quote = L'"';
        std::vector<Span> cont;
        const std::wstring line = L"it's still open\" then code = 4;";
        computeSpans(line.c_str(), line.size(), open, cont);
        check(open.mode == LexState::Mode::Normal, "the matching quote closes it");
        check(classAt(cont, 3) == SpanClass::String, "the apostrophe inside did not");
        check(classAt(cont, line.find(L"4;")) == SpanClass::Number, "and code resumes");

        // THE MIRROR CASE, and it is the one that earns its keep. Everything above resumes
        // with quote == '"', so a lexer whose resume branch hardcoded L'"' instead of
        // reading state.quote would pass every assertion so far. Resume inside a SINGLE
        // quoted run over a line containing a double quote: the " must NOT close it, and the
        // ' must. Verified to fail against exactly that injected fault.
        LexState sq;
        sq.mode = LexState::Mode::StringLit;
        sq.quote = L'\'';
        std::vector<Span> scont;
        const std::wstring sline = L"say \"hi\" then' code = 7;";
        computeSpans(sline.c_str(), sline.size(), sq, scont);
        check(classAt(scont, sline.find(L'"')) == SpanClass::String,
              "a double quote does not close a single-quoted run");
        check(sq.mode == LexState::Mode::Normal, "the matching single quote does");
        check(classAt(scont, sline.find(L"7;")) == SpanClass::Number,
              "and code resumes after it");
    }

    printf("\n6. char literals take the same path as strings (one rule, as before)\n");
    {
        const std::wstring s = L"let k: secret u8 = 'k'; let z = '\\n';";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, s.find(L"'k'")) == SpanClass::String, "'k' is a string span");
        check(classAt(v, s.find(L"'k'") + 2) == SpanClass::String, "including its closing quote");
        check(classAt(v, s.find(L"secret")) == SpanClass::Keyword, "secret is still a keyword");
        check(classAt(v, s.find(L"u8")) == SpanClass::Keyword, "and so is the type name u8");
        check(classAt(v, s.find(L"'\\n'") + 1) == SpanClass::String, "an escape inside it is fine");
        check(spansWellFormed(v, s.size()), "spans well-formed");
    }

    printf("\n7. numbers absorb '.', '_' and trailing word characters\n");
    {
        const std::wstring s = L"1_000.5 0xFF 3f32 42";
        const std::vector<Span> v = spansOf(s);
        check(v.size() == 4, "four number spans");
        check(v[0].begin == 0 && v[0].end == 7 && v[0].cls == SpanClass::Number,
              "1_000.5 is ONE span (both '.' and '_' continue it)");
        check(v[1].end - v[1].begin == 4, "0xFF is one span");
        check(v[2].end - v[2].begin == 4, "3f32 is one span (a suffix does not split it)");
        check(v[3].end == s.size(), "42 runs to the end of the buffer");
        // The leading character has to be a DIGIT: _foo and foo are identifiers.
        const std::wstring id = L"_1 x2";
        check(spansOf(id).empty(), "_1 and x2 are identifiers, not numbers");
    }

    printf("\n8. identifiers versus keywords\n");
    {
        const std::wstring s = L"fn declassify_it(secret_x: u8) { let mut y = x; }";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, 0) == SpanClass::Keyword, "fn is a keyword");
        check(classAt(v, s.find(L"declassify_it")) == SpanClass::Default,
              "declassify_it is NOT (a keyword must be the whole word)");
        check(classAt(v, s.find(L"secret_x")) == SpanClass::Default, "nor is secret_x");
        check(classAt(v, s.find(L"u8")) == SpanClass::Keyword, "u8 is");
        check(classAt(v, s.find(L"let")) == SpanClass::Keyword, "let is");
        check(classAt(v, s.find(L"mut")) == SpanClass::Keyword, "mut is");
        check(classAt(v, s.find(L" y =") + 1) == SpanClass::Default, "y is not");
        check(isKeyword(L"declassify", 10), "isKeyword: Sentinel's own words are in the set");
        check(isKeyword(L"secret", 6) && isKeyword(L"effect", 6) && isKeyword(L"capability", 10),
              "isKeyword: secret / effect / capability");
        check(!isKeyword(L"declassify", 4), "isKeyword respects the length (decl != declassify)");
        check(!isKeyword(nullptr, 3) && !isKeyword(L"fn", 0), "isKeyword: null / empty are safe");
    }

    printf("\n9. non-ASCII identifiers do not derail the scan\n");
    {
        // Sentinel sources really do carry these, and EditorModel's word-class predicate
        // was widened (vendoring change #1) to agree with this lexer's. The assertion
        // below is written against iswalnum RATHER than a hard-coded expectation, because
        // agreeing with the CRT predicate IS the property: if these two ever disagree,
        // double-click selection and the highlighter disagree about where a word ends.
        const std::wstring s = L"let ünïcode_id = 42;";
        const std::vector<Span> v = spansOf(s);
        check(classAt(v, 0) == SpanClass::Keyword, "let is still found");
        check(classAt(v, s.find(L"42")) == SpanClass::Number, "and the number after it");
        check(classAt(v, s.find(L"ü")) == SpanClass::Default,
              "a non-ASCII identifier is not a keyword");
        check(spansWellFormed(v, s.size()), "spans well-formed");

        const bool nonAsciiIsWord = iswalnum(static_cast<wint_t>(L'ü')) != 0;
        printf("     iswalnum(U+00FC) = %d (the word-class predicate both units share)\n",
               nonAsciiIsWord ? 1 : 0);
        const std::wstring glued = L"secretü";
        const bool sawKeyword = !spansOf(glued).empty();
        check(sawKeyword == !nonAsciiIsWord,
              "'secret'+U+00FC is one identifier exactly when U+00FC is a word character");
    }

    printf("\n10. degenerate inputs\n");
    {
        LexState st;
        check(spansOf(L"").empty(), "empty buffer: no spans");
        std::vector<Span> v;
        computeSpans(L"", 0, st, v);
        check(v.empty() && st.mode == LexState::Mode::Normal, "empty buffer: state unchanged");
        computeSpans(nullptr, 0, st, v);
        check(v.empty(), "null buffer is not a crash");
        // An empty LINE in the middle of a block comment must leave the state alone --
        // this is the case the Direct2D control hits on every blank line of a comment.
        LexState open;
        open.mode = LexState::Mode::BlockComment;
        computeSpans(L"", 0, open, v);
        check(open.mode == LexState::Mode::BlockComment && v.empty(),
              "an empty line inside a block comment keeps the state");
        check(spansOf(L"/").empty(), "a lone / is nothing");
        check(spansOf(L"/*").size() == 1, "a lone /* is a comment to the end");
        check(spansOf(L"\"").size() == 1, "a lone quote is a string to the end");
        check(spansOf(L"      \t  ").empty(), "whitespace only");
    }

    printf("\n11. span integrity across every shape above\n");
    {
        const std::wstring cases[] = {
            L"",
            L"// only a comment",
            L"/* unterminated",
            L"/* a */ /* b */ /* c",
            L"\"unterminated",
            L"'a' 'b' 'c'",
            L"1 2 3 4.5_6",
            L"fn main() -> i64 { let x: secret u8 = 'k'; }",
            L"////\n****\n\"\"\"\"\n''''",
            L"a\\\nb",
            L"\"esc\\\"\" 1 // x\n/* y */ 'z'",
        };
        bool allOk = true;
        for (const std::wstring& c : cases) {
            if (!spansWellFormed(spansOf(c), c.size())) {
                printf("     malformed spans for: %ls\n", c.c_str());
                allOk = false;
            }
        }
        check(allOk, "ordered, non-overlapping, non-empty, in bounds, never Default");
    }

    printf("\n12. PER-LINE == WHOLE-BUFFER (the property the Direct2D control rides on)\n");
    {
        // Every one of these straddles a line boundary in a different way. If any pair
        // disagrees, the two editors colour the same file differently -- and only one of
        // them would be looked at.
        check(sameEitherWay(L"// c\nlet a = 1;\n", "plain"), "plain lines");
        check(sameEitherWay(L"let a = /* one\ntwo\nthree */ 7;\n", "block"),
              "a block comment spanning three lines");
        check(sameEitherWay(L"/* open\nlet b = 2;\n", "block-open"),
              "a block comment that never closes");
        check(sameEitherWay(L"let s = \"open\nstill\" ok;\n", "string-open"),
              "a string that closes on a later line");
        check(sameEitherWay(L"let s = \"never\nlet b = 2;\n", "string-never"),
              "a string that never closes");
        // THE nasty one: the last character of the line is a backslash, so in the whole
        // buffer it escapes the '\n' itself. Per line there is no '\n' to escape -- and
        // both must still leave the next line inside the string with no escape pending.
        check(sameEitherWay(L"let s = \"tail\\\nnext\" ok;\n", "escaped-newline"),
              "a backslash escaping the line break");
        check(sameEitherWay(L"/*\n*\n/\n*/ x = 1;\n", "star-slash-split"),
              "'*' and '/' on either side of a line break do NOT close a comment");
        check(sameEitherWay(L"// c1\n// c2\n\n/* b */\n\nlet x = 'q';\n", "mixed"),
              "blank lines, mixed comment forms");
        check(sameEitherWay(L"", "empty"), "an empty buffer");
        check(sameEitherWay(L"\n\n\n", "blank"), "blank lines only");
        check(sameEitherWay(L"no trailing newline = 1", "no-trailing"), "no trailing newline");
    }

    printf("\n13. the shape MainWindow::highlight() actually sees\n");
    {
        // The RichEdit buffer's line break is a LONE CR, not LF, and highlight() lexes it
        // in one go. Same rules, same answers -- this pins that the extraction did not
        // quietly assume EditorModel's LF representation.
        const std::wstring cr = L"// header\rfn main() -> i64 {\r  let k = 'k';\r}\r";
        const std::vector<Span> v = spansOf(cr);
        check(classAt(v, 0) == SpanClass::Comment, "the header comment");
        check(classAt(v, cr.find(L"fn")) == SpanClass::Keyword, "fn on the next CR-line");
        check(classAt(v, cr.find(L"i64")) == SpanClass::Keyword, "i64");
        check(classAt(v, cr.find(L"'k'")) == SpanClass::String, "'k'");
        check(classAt(v, cr.find(L"main")) == SpanClass::Default, "main is not a keyword");
        check(spansWellFormed(v, cr.size()), "spans well-formed");
    }

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
