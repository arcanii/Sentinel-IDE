// SPDX-License-Identifier: GPL-3.0-or-later
// SyntaxLexer.cpp — see SyntaxLexer.h. Lifted from MainWindow.cpp::highlight() (phase 46
// slice 4) with its behaviour preserved character for character, then given a start state.
//
// ONE implementation, three entry points. computeSpans and advanceState both call
// lexChunk; the only difference is whether the span vector is there to be filled. A second
// state-only scan would be a second copy of the rules, which is the exact failure this
// whole file exists to prevent.
#include "editor/SyntaxLexer.h"

#include <cwctype>
#include <string_view>
#include <unordered_set>

namespace editor {

namespace {

// The keyword set, moved here verbatim from MainWindow.cpp::keywords(). Sentinel's own
// words (secret / effect / capability / declassify …) sit alongside the Rust-shaped
// spine and the primitive type names, which is how highlight() has always spelled it.
//
// wstring_view, not wstring: the Direct2D painter looks up every identifier on every
// visible line on every paint, and a std::wstring temporary per identifier would be an
// allocation per word per frame. The set's storage is the string literals themselves,
// which have static lifetime, so the views can never dangle.
const std::unordered_set<std::wstring_view>& keywordSet() {
    static const std::unordered_set<std::wstring_view> k = {
        L"fn",L"let",L"mut",L"const",L"static",L"return",L"if",L"else",L"for",L"while",
        L"loop",L"match",L"struct",L"enum",L"impl",L"trait",L"pub",L"use",L"mod",L"type",
        L"as",L"in",L"break",L"continue",L"true",L"false",L"self",L"Self",L"where",L"move",
        L"ref",L"async",L"await",L"unsafe",L"extern",L"crate",L"super",L"dyn",
        L"secret",L"effect",L"effects",L"borrow",L"capability",L"cap",L"declassify",
        L"u8",L"u16",L"u32",L"u64",L"usize",L"i8",L"i16",L"i32",L"i64",L"isize",
        L"f32",L"f64",L"bool",L"char",L"str",L"void",
    };
    return k;
}

// The identifier predicates, kept as three separate one-liners so they read the same way
// they did inline. iswalnum/iswalpha are the LOCALE-AWARE forms on purpose: Sentinel
// sources really do carry non-ASCII identifiers, and EditorModel's word-class predicate
// (vendoring change #1) was widened to match this one — the two must keep agreeing or
// double-click selection and the highlighter disagree about where a word ends.
bool isDigitCh(wchar_t c) { return iswdigit(c) != 0; }
bool isAlphaCh(wchar_t c) { return iswalpha(c) != 0; }
bool isWordCh(wchar_t c) { return iswalnum(c) != 0 || c == L'_'; }

// Scan a block comment from `from`; returns its end and whether "*/" actually closed it.
// The arithmetic is highlight()'s: `j = (j + 2 <= len) ? j + 2 : len`, restated as a
// closed/open answer because the caller now has to record the open case in LexState.
// They agree in every case — the loop can only exit on a match (where j + 2 <= len holds
// by construction) or with j + 1 >= len (where the original also collapses to len).
size_t scanBlockComment(const wchar_t* s, size_t n, size_t from, bool& closed) {
    size_t j = from;
    while (j + 1 < n && !(s[j] == L'*' && s[j + 1] == L'/')) ++j;
    closed = (j + 1 < n);
    return closed ? j + 2 : n;
}

// Scan a string or char literal from `from` up to the closing `q`. NOTE WHAT IS ABSENT:
// there is no newline test, so an unterminated string swallows everything after it. That
// is what highlight() has always done and it is deliberately preserved — it is also the
// second reason LexState exists, since the swallow crosses lines.
size_t scanString(const wchar_t* s, size_t n, size_t from, wchar_t q, bool& closed) {
    size_t j = from;
    while (j < n && s[j] != q) {
        if (s[j] == L'\\' && j + 1 < n)
            j += 2;  // an escape consumes the next code unit, whatever it is
        else
            ++j;
    }
    closed = (j < n);
    return closed ? j + 1 : n;
}

void lexChunk(const wchar_t* s, size_t n, LexState& state, std::vector<Span>* out) {
    const auto emit = [out](size_t a, size_t b, SpanClass cls) {
        if (out && b > a) out->push_back(Span{a, b, cls});
    };
    size_t i = 0;

    // ---- resume whatever the previous chunk left open ------------------------
    // Only reachable when a caller lexes line by line; a whole-buffer caller passes the
    // default state and falls straight through.
    if (state.mode == LexState::Mode::BlockComment) {
        bool closed = false;
        const size_t end = scanBlockComment(s, n, 0, closed);
        emit(0, end, SpanClass::Comment);
        if (!closed) return;  // still open at the end of this chunk; state unchanged
        state.mode = LexState::Mode::Normal;
        i = end;
    } else if (state.mode == LexState::Mode::StringLit) {
        bool closed = false;
        const size_t end = scanString(s, n, 0, state.quote, closed);
        emit(0, end, SpanClass::String);
        if (!closed) return;
        state.mode = LexState::Mode::Normal;
        state.quote = 0;
        i = end;
    }

    // ---- the rules, in highlight()'s order (which is also their precedence) ---
    while (i < n) {
        const wchar_t c = s[i];

        if (c == L'/' && i + 1 < n && s[i + 1] == L'/') {  // line comment
            size_t j = i;
            // '\r' as well as '\n', because the RichEdit buffer's line break is a lone CR
            // while EditorModel's is LF. One rule, both representations.
            while (j < n && s[j] != L'\r' && s[j] != L'\n') ++j;
            emit(i, j, SpanClass::Comment);
            i = j;
            continue;
        }
        if (c == L'/' && i + 1 < n && s[i + 1] == L'*') {  // block comment
            bool closed = false;
            const size_t end = scanBlockComment(s, n, i + 2, closed);
            emit(i, end, SpanClass::Comment);
            i = end;
            if (!closed) {
                state.mode = LexState::Mode::BlockComment;
                return;
            }
            continue;
        }
        if (c == L'"' || c == L'\'') {  // string OR char literal — one rule, as before
            bool closed = false;
            const size_t end = scanString(s, n, i + 1, c, closed);
            emit(i, end, SpanClass::String);
            i = end;
            if (!closed) {
                state.mode = LexState::Mode::StringLit;
                state.quote = c;
                return;
            }
            continue;
        }
        if (isDigitCh(c)) {  // number: leading digit, then word chars, '.' or '_'
            size_t j = i;
            while (j < n && (isWordCh(s[j]) || s[j] == L'.')) ++j;
            emit(i, j, SpanClass::Number);
            i = j;
            continue;
        }
        if (isAlphaCh(c) || c == L'_') {  // identifier; only keywords get a span
            size_t j = i;
            while (j < n && isWordCh(s[j])) ++j;
            if (isKeyword(s + i, j - i)) emit(i, j, SpanClass::Keyword);
            i = j;
            continue;
        }
        ++i;
    }
}

}  // namespace

bool isKeyword(const wchar_t* word, size_t len) {
    if (!word || len == 0) return false;
    return keywordSet().count(std::wstring_view(word, len)) != 0;
}

void computeSpans(const wchar_t* text, size_t len, LexState& state, std::vector<Span>& out) {
    out.clear();
    if (!text) return;
    lexChunk(text, len, state, &out);
}

std::vector<Span> computeSpans(const std::wstring& text, LexState& state) {
    std::vector<Span> out;
    computeSpans(text.c_str(), text.size(), state, out);
    return out;
}

void advanceState(const wchar_t* text, size_t len, LexState& state) {
    if (!text) return;
    lexChunk(text, len, state, nullptr);
}

}  // namespace editor
