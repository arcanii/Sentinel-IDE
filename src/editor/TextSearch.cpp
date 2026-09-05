// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/TextSearch.h"

#include <cwctype>

namespace editor {

namespace {

bool isHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool isLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// The same predicate EditorModel::isWordChar uses — deliberately the same, not merely
// similar. See the header.
bool isWordChar(wchar_t c) { return iswalnum(c) != 0 || c == L'_'; }

// Would a boundary at `off` cut a surrogate pair in half? Character-for-character
// EditorModel::snap's test, which is what makes "a match is never snapped" and "the caret
// is never snapped" the same rule rather than two rules that agree today.
bool splitsPair(const std::wstring& s, size_t off) {
    return off > 0 && off < s.size() && isHighSurrogate(s[off - 1]) && isLowSurrogate(s[off]);
}

}  // namespace

wchar_t foldCase(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + 32);
    // Latin-1 supplement. U+00D7 is ×, a maths operator sitting inside the letter block,
    // and folding it would make × match ÷ (U+00F7) — visibly wrong and easy to miss.
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return static_cast<wchar_t>(c + 32);
    return c;
}

void findAll(const std::wstring& hay, const std::wstring& needle,
             const SearchOptions& opt, std::vector<Range>& out) {
    out.clear();
    const size_t n = hay.size(), m = needle.size();
    if (m == 0 || m > n) return;

    // Fold the needle ONCE. It is re-scanned for every candidate position, so folding it
    // in the inner loop would pay for the same characters n times.
    std::wstring pat = needle;
    if (!opt.matchCase)
        for (wchar_t& c : pat) c = foldCase(c);
    const wchar_t p0 = pat[0];

    size_t i = 0;
    while (i + m <= n) {
        const wchar_t h0 = opt.matchCase ? hay[i] : foldCase(hay[i]);
        if (h0 != p0) {
            ++i;
            continue;
        }
        bool eq = true;
        for (size_t k = 1; k < m; ++k) {
            const wchar_t a = opt.matchCase ? hay[i + k] : foldCase(hay[i + k]);
            if (a != pat[k]) {
                eq = false;
                break;
            }
        }
        const size_t end = i + m;
        if (eq && opt.wholeWord) {
            if ((i > 0 && isWordChar(hay[i - 1])) || (end < n && isWordChar(hay[end])))
                eq = false;
        }
        // The surrogate guard is LAST because it is the rarest and the most expensive to
        // reason about, not because it is the least important — a match that fails it is
        // discarded, never trimmed. See the header.
        if (eq && (splitsPair(hay, i) || splitsPair(hay, end))) eq = false;

        if (eq) {
            out.push_back({ i, end });
            i = end;  // non-overlapping
        } else {
            ++i;
        }
    }
}

size_t matchAtOrAfter(const std::vector<Range>& matches, size_t from) {
    for (size_t i = 0; i < matches.size(); ++i)
        if (matches[i].start >= from) return i;
    return 0;  // past the last match -> wrap to the first
}

size_t matchBefore(const std::vector<Range>& matches, size_t from) {
    if (matches.empty()) return 0;
    for (size_t i = matches.size(); i > 0; --i)
        if (matches[i - 1].end <= from) return i - 1;
    return matches.size() - 1;  // wrap to the last
}

}  // namespace editor
