// SPDX-License-Identifier: GPL-3.0-or-later
// editor_model_test — the unit-tested core of the coming Direct2D editor.
//
// Build+run:  cmake --build build --target editor_model_test && build\editor_model_test.exe
//
// src/editor/EditorModel.{h,cpp} is vendored from G:\SQLTerminal-Win32 (same author,
// GPL-3.0, same lineage) with exactly one behavioural change: the word-class predicate is
// Sentinel's `iswalnum(c) || c == '_'` rather than SQLTerminal's ASCII-only [A-Za-z0-9_],
// so word navigation agrees with what MainWindow.cpp::highlight calls an identifier.
//
// The model is pure text — no Windows, no Direct2D — so the bulk of the editor's behaviour
// can be pinned here, before a single pixel exists. That ordering is deliberate: the editor
// swap's two ways of losing a user's work are a missed change notification and a save that
// writes LF where the file had CRLF, and the second is provable right here.
//
// CASE 10 IS THE ONE THAT MATTERS MOST. examples/crypto.sentinel is a COMMITTED SIGNED
// demo (426 bytes, 12 CRLF, with crypto.sentinel.sig beside it), it is the file that opens
// by DEFAULT when you open examples/, and Build AUTO-SAVES the open file. So a model that
// does not round-trip it byte-for-byte would invalidate its signature without anyone ever
// pressing Ctrl+S. That is why the golden case reads the real file rather than a literal.
#include "editor/EditorModel.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace editor;

static int gPass = 0, gFail = 0;
static void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

static std::string readFileBytes(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    DWORD n = GetFileSize(f, nullptr), got = 0;
    std::string s(n, '\0');
    ReadFile(f, s.data(), n, &got, nullptr);
    CloseHandle(f);
    s.resize(got);
    return s;
}
static std::wstring toW(const std::string& u8) {
    if (u8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), w.data(), n);
    return w;
}
static std::string toU8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

int main(int argc, char** argv) {
    // The golden file's path comes from the build so the test is not tied to a CWD.
    const std::wstring goldenPath = (argc > 1) ? toW(argv[1])
                                              : L"G:\\SentinelIDE\\examples\\crypto.sentinel";
    printf("EditorModel\n\n");

    printf("1. normalizeNewlines collapses every line-break form to LF\n");
    {
        check(normalizeNewlines(L"a\r\nb") == L"a\nb",          "CRLF -> LF");
        check(normalizeNewlines(L"a\rb")   == L"a\nb",          "lone CR -> LF");
        check(normalizeNewlines(L"a\nb")   == L"a\nb",          "LF unchanged");
        check(normalizeNewlines(L"a\r\n\r\nb") == L"a\n\nb",    "consecutive CRLF");
        check(normalizeNewlines(L"a\r")    == L"a\n",           "trailing lone CR");
        check(normalizeNewlines(L"")       == L"",              "empty");
    }

    printf("\n2. setText normalizes, parks the caret at the end, clears history\n");
    {
        EditorModel m;
        m.setText(L"one\r\ntwo");
        check(m.text() == L"one\ntwo",   "stored with LF only");
        check(m.caret() == m.length(),   "caret at end (matches WM_SETTEXT)");
        check(!m.canUndo(),              "undo history cleared, as WM_SETTEXT does");
    }

    printf("\n3. textCrlf round-trips back to the on-disk form\n");
    {
        EditorModel m;
        m.setText(L"a\r\nb\r\n");
        check(m.text()     == L"a\nb\n",     "internal form is LF");
        check(m.textCrlf() == L"a\r\nb\r\n", "external form is CRLF");
        m.setText(L"no breaks");
        check(m.textCrlf() == L"no breaks",  "no breaks: unchanged");
        m.setText(L"");
        check(m.textCrlf() == L"",           "empty: unchanged");
    }

    printf("\n4. insert / backspace / deleteForward\n");
    {
        EditorModel m;
        m.setText(L"abc");
        m.setCaret(1, false);
        m.insertText(L"X");
        check(m.text() == L"aXbc",  "insert at caret");
        check(m.caret() == 2,       "caret advances past the insert");
        m.backspace();
        check(m.text() == L"abc",   "backspace removes it again");
        m.setCaret(0, false);
        m.deleteForward();
        check(m.text() == L"bc",    "deleteForward at doc start");
    }

    printf("\n5. selection replace\n");
    {
        EditorModel m;
        m.setText(L"hello world");
        m.setSelection(0, 5);
        m.insertText(L"bye");
        check(m.text() == L"bye world", "typing over a selection replaces it");
        m.selectAll();
        m.deleteSelection();
        check(m.text().empty(),         "selectAll + delete empties the buffer");
    }

    printf("\n6. undo/redo restores text AND selection\n");
    {
        EditorModel m;
        m.setText(L"abc");
        m.setSelection(1, 3);
        m.insertText(L"Z");
        check(m.text() == L"aZ", "edit applied");
        m.undo();
        check(m.text() == L"abc",            "undo restored the text");
        check(m.selection().min() == 1 && m.selection().max() == 3,
                                             "undo also restored the selection");
        m.redo();
        check(m.text() == L"aZ", "redo re-applied");
    }

    printf("\n7. a typing run coalesces into one undo step\n");
    {
        EditorModel m;
        m.setText(L"");
        m.insertText(L"a"); m.insertText(L"b"); m.insertText(L"c");
        check(m.text() == L"abc", "three inserts");
        m.undo();
        check(m.text() != L"ab", "one undo does not step back a single character");
        check(m.text().empty(),  "the whole typing run is one step");
    }

    printf("\n8. lineStart / lineEnd on LF boundaries\n");
    {
        EditorModel m;
        m.setText(L"aa\nbbb\nc");
        check(m.lineStart(0) == 0, "start of first line");
        check(m.lineStart(4) == 3, "start of second line");
        check(m.lineEnd(4)   == 6, "end of second line is at the LF");
        check(m.lineEnd(8)   == 8, "end of last line is the doc end");
    }

    printf("\n9. surrogate pairs are never split\n");
    {
        EditorModel m;
        m.setText(L"a\xD83D\xDE00 b");           // a, U+1F600, space, b
        m.setCaret(2, false);                     // between the surrogates
        check(m.caret() != 2, "caret snapped off the surrogate boundary");
        m.setCaret(1, false);
        const size_t r = m.stepCodepoint(1, +1);
        check(r == 3, "stepCodepoint crosses the whole pair, not one unit");
    }

    printf("\n10. GOLDEN: the committed signed demo round-trips byte-for-byte\n");
    {
        const std::string disk = readFileBytes(goldenPath.c_str());
        check(!disk.empty(), "read examples/crypto.sentinel");
        if (!disk.empty()) {
            EditorModel m;
            m.setText(toW(disk));
            const std::string out = toU8(m.textCrlf());
            check(out.size() == disk.size(),
                  "byte count unchanged (a CRLF->LF slip would shrink it)");
            check(out == disk,
                  "BYTE-IDENTICAL -- crypto.sentinel.sig stays valid");
            size_t cr = 0;
            for (char c : out) if (c == '\r') ++cr;
            check(cr == 12, "all 12 CRLF survived the round trip");
        }
    }

    printf("\n11. hostile shapes still round-trip\n");
    {
        EditorModel m;
        m.setText(L"no trailing newline");
        check(m.textCrlf() == L"no trailing newline", "no trailing newline");

        std::wstring big;
        for (int i = 0; i < 8000; ++i) big += L"line of source text\r\n";
        m.setText(big);
        check(m.textCrlf() == big, "160k of CRLF text round-trips (phase 17's tail-truncation shape)");

        m.setText(L"\r\n\r\n\r\n");
        check(m.textCrlf() == L"\r\n\r\n\r\n", "blank lines only");
    }

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
