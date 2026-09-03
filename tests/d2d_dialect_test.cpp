// SPDX-License-Identifier: GPL-3.0-or-later
// d2d_dialect_test — the RichEdit MESSAGE DIALECT test for the Direct2D editor control
// (phase 46 slice 3).
//
// Build+run:  cmake --build build --target d2d_dialect_test && build\d2d_dialect_test.exe
// or via ctest:  ctest --test-dir build -R d2d_dialect
//
// WHY THIS TEST EXISTS, and it is not the same reason as d2d_render_test's.
// tests/editor_model_test.cpp pins `setText -> textCrlf()` — the pure model. But the bytes
// that actually reach disk do not come from textCrlf(); they come from the HANDLERS'
// arithmetic on the way to it. saveFile (MainWindow.cpp:797-819) asks
// EM_GETTEXTLENGTHEX{GTL_NUMCHARS}, sizes a buffer `n*2 + 16`, then asks EM_GETTEXTEX with
// `cb` in BYTES and expects a CHARACTER count back; the handler divides by
// sizeof(wchar_t) and reserves one code unit for the terminator. Not one of those
// operations was covered by anything.
//
// The failure that buys: read `cb` as a CHARACTER count, or drop the `- 1`, and ctest stays
// green while the first symptom in the wild is examples/crypto.sentinel — a COMMITTED
// SIGNED file that opens by DEFAULT — written short or LF-only by the BUILD'S AUTO-SAVE,
// with nobody having pressed Ctrl+S. The `cb` slip is additionally a heap overrun in
// editorText(), whose buffer is exactly n+1 wchar_t and which runs on EVERY keystroke.
// Case 3 is the one that catches those, with a canary past the declared capacity.
//
// THE OTHER ASSERTION THIS FILE EXISTS FOR is case 8: EN_CHANGE must be SYNCHRONOUS. A
// regression from SendMessageW to PostMessageW is failure mode #1 for the whole migration —
// loadFileIntoEditor and closeProject clear g.loadingFile on the next line with no pump
// between, so a POSTED notification escapes the guard and is processed as a user edit,
// while a posted notification for a REAL edit can be swallowed by that guard inside a
// modal's pump after confirmSaveIfDirty already asked. Both end in a discarded buffer. The
// check is exact: the parent's counter must have moved by the time SendMessageW RETURNS,
// with no message pump anywhere in between, and nothing may be left in the queue.
//
// DELIBERATELY NOT BRITTLE. Everything here is a count, a range or an invariant —
// byte-identity against the file on disk, "never negative", "the two are inverses", "did
// not write past the declared capacity". There is not one pixel, glyph position or font
// metric in the file, so a different font, DPI or theme cannot break it. Case 7 is the
// closest it comes, and it uses three IDENTICAL lines precisely so the answer is
// font-independent, and asserts a range rather than a column.
//
// SKIP, LOUDLY, is reserved for one thing (as in d2d_render_test): an environment with no
// Direct2D/DirectWrite at all. A control that IS created and answers wrongly is a FAILURE.
//
// READ-ONLY on examples/: the golden input is opened GENERIC_READ and this file has no
// write path to it whatsoever. crypto.sentinel is committed and SIGNED.
//
// windows.h BEFORE richedit.h, exactly as MainWindow.cpp does it — richedit.h guards
// EM_POSFROMCHAR and EM_SCROLLCARET with #ifndef, so header order decides their numeric
// value. Getting it wrong here would send DIFFERENT messages than the host sends and the
// test would be measuring a dialect nobody speaks. See the same note in D2DEditor.cpp.
#include <windows.h>

#include <richedit.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "host/win32/D2DEditor.h"
#include "host/win32/D2DSupport.h"

namespace {

int gPass = 0, gFail = 0;
void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

// ---- the parent, which is half the test ------------------------------------
// The funnel returns early when GetParent() is null, so a parentless control reports
// NOTHING and every notification assertion below would vacuously pass. This host is a real
// window with a real WndProc, and the control is a real WS_CHILD with a real control id —
// the same three things MainWindow provides.
constexpr const wchar_t* kHostClass = L"SentinelD2DDialectHost";
constexpr int kEditId = 1000;  // stands in for IDC_EDIT

struct Counts {
    int change = 0, vscroll = 0, hscroll = 0, selchange = 0;
    LONG selMin = -1, selMax = -1;
    WORD selTyp = 0;
};
Counts gC;

// The SAME dispatch shape as MainWindow.cpp: WM_COMMAND carries EN_CHANGE / EN_VSCROLL /
// EN_HSCROLL and tests `lParam != 0` to tell a control notification from a menu command;
// WM_NOTIFY carries EN_SELCHANGE with a SELCHANGE payload. Matching it is the point — if
// the control ever sent EN_SELCHANGE as a WM_COMMAND (it has no WM_COMMAND form), it would
// fall out of the host's handler silently and the Ln/Col readout would freeze forever, so
// this host must be exactly as picky as the real one.
LRESULT CALLBACK HostProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND && lParam != 0 && LOWORD(wParam) == kEditId) {
        switch (HIWORD(wParam)) {
            case EN_CHANGE: ++gC.change; return 0;
            case EN_VSCROLL: ++gC.vscroll; return 0;
            case EN_HSCROLL: ++gC.hscroll; return 0;
            default: break;
        }
    }
    if (msg == WM_NOTIFY) {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm && nm->idFrom == static_cast<UINT_PTR>(kEditId) && nm->code == EN_SELCHANGE) {
            auto* sc = reinterpret_cast<SELCHANGE*>(lParam);
            ++gC.selchange;
            gC.selMin = sc->chrg.cpMin;
            gC.selMax = sc->chrg.cpMax;
            gC.selTyp = sc->seltyp;
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wParam, lParam);
}

// Throw away anything sitting in this thread's queue WITHOUT dispatching it. Dispatching
// would be self-defeating: a notification that had been POSTED instead of sent would get
// delivered by the drain and the counter would look right. Every synchronicity assertion
// below drains first, sends once, and then requires BOTH that the counter moved AND that
// the queue is still empty.
void drainQueueUndispatched() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    }
}

bool queueHasCommandFor(HWND host) {
    MSG m;
    return PeekMessageW(&m, host, WM_COMMAND, WM_COMMAND, PM_NOREMOVE) != FALSE;
}

// ---- file + encoding helpers (same shape as editor_model_test) --------------

std::wstring toW(const char* s) {
    if (!s || !*s) return std::wstring();
    const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 1) return std::wstring();
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

std::wstring utf8ToW(const std::string& u8) {
    if (u8.empty()) return std::wstring();
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), w.data(), n);
    return w;
}

std::string wToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}

// GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING. There is no counterpart that writes.
std::string readFileBytes(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    DWORD n = GetFileSize(f, nullptr), got = 0;
    std::string s(n, '\0');
    if (n) ReadFile(f, s.data(), n, &got, nullptr);
    CloseHandle(f);
    s.resize(got);
    return s;
}

size_t countCh(const std::wstring& s, wchar_t c) {
    size_t n = 0;
    for (wchar_t ch : s) {
        if (ch == c) ++n;
    }
    return n;
}

size_t countCh(const std::string& s, char c) {
    size_t n = 0;
    for (char ch : s) {
        if (ch == c) ++n;
    }
    return n;
}

// ---- the host's own call sequences, copied verbatim in shape ---------------
// These are not convenience wrappers invented for the test. Each is the literal arithmetic
// of the MainWindow.cpp function named in its comment, so a change to the dialect that
// breaks the host breaks this, and a change that does not, does not.

// MainWindow.cpp::saveFile (:799-806) — the sequence whose output is written to disk.
std::wstring saveFileFetch(HWND edit, LONG& nOut, LONG& gotOut) {
    GETTEXTLENGTHEX gtl{GTL_NUMCHARS, 1200};
    LONG n = static_cast<LONG>(SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
    std::wstring s;
    s.resize(static_cast<size_t>(n) * 2 + 16);
    GETTEXTEX gt{};
    gt.cb = static_cast<DWORD>(s.size() * sizeof(wchar_t));
    gt.flags = GT_USECRLF;
    gt.codepage = 1200;
    LONG got = static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data()));
    s.resize(got < 0 ? 0 : static_cast<size_t>(got));
    nOut = n;
    gotOut = got;
    return s;
}

// MainWindow.cpp::editorText (:542-547) — the internal form g.dirty is computed from, and
// the one whose buffer is exactly n+1 wchar_t. It runs on every EN_CHANGE.
std::wstring editorTextFetch(HWND edit, LONG& nOut, LONG& gotOut) {
    GETTEXTLENGTHEX gtl{GTL_NUMCHARS, 1200};
    LONG n = static_cast<LONG>(SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
    std::wstring s;
    s.resize(static_cast<size_t>(n) + 1);
    GETTEXTEX gt{};
    gt.cb = static_cast<DWORD>((static_cast<size_t>(n) + 1) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    LONG got = static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data()));
    s.resize(got < 0 ? 0 : static_cast<size_t>(got));
    nOut = n;
    gotOut = got;
    return s;
}

LONG lengthEx(HWND edit, DWORD flags) {
    GETTEXTLENGTHEX gtl{flags, 1200};
    return static_cast<LONG>(SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
}

LONG lineIndex(HWND edit, LONG line) {
    return static_cast<LONG>(SendMessageW(edit, EM_LINEINDEX, static_cast<WPARAM>(line), 0));
}

LONG lineFromChar(HWND edit, LONG off) {
    return static_cast<LONG>(SendMessageW(edit, EM_EXLINEFROMCHAR, 0, static_cast<LPARAM>(off)));
}

void setSel(HWND edit, LONG a, LONG b) {
    CHARRANGE cr{a, b};
    SendMessageW(edit, EM_EXSETSEL, 0, (LPARAM)&cr);
}

CHARRANGE getSel(HWND edit) {
    CHARRANGE cr{-999, -999};
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    return cr;
}

}  // namespace

int main(int argc, char** argv) {
    // The golden file's path comes from the build, like editor_model_test's, so the test
    // does not depend on a working directory.
    const std::wstring goldenPath =
        (argc > 1) ? toW(argv[1]) : std::wstring(L"G:\\SentinelIDE\\examples\\crypto.sentinel");

    printf("D2DEditor RichEdit dialect\n\n");

    // Match the demo host and d2d_render_test: no .rc here, so per-monitor-v2 has to be
    // asserted in code or GetDpiForWindow reports a lie.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    printf("0. the environment has Direct2D and DirectWrite\n");
    {
        // The dialect messages themselves need no device, but case 7 navigates by column,
        // which goes through DirectWrite hit-testing. No DWrite, no meaningful test — and a
        // loud skip is the honest answer, not a red result blaming the editor.
        if (!sentinelide::d2dFactory() || !sentinelide::dwriteFactory()) {
            printf("  [SKIP] %s\n", sentinelide::d2dFactory()
                                        ? "DWriteCreateFactory returned nothing"
                                        : "D2D1CreateFactory returned nothing");
            printf("\nSKIPPED: this environment has no Direct2D/DirectWrite, so the control\n"
                   "         is untestable here. This is NOT a pass - re-run somewhere with\n"
                   "         Direct2D available before trusting the dialect.\n");
            return 0;
        }
        check(true, "D2D + DWrite factories");
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kHostClass;
    const bool hostClassOk =
        RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    check(hostClassOk, "registered the parent host class");
    check(sentinelide::registerD2DEditorClass(hInst), "registered SentinelD2DEditor");

    // Hidden, never shown, but REAL: a parent to receive notifications and a child with a
    // control id, WS_VSCROLL|WS_HSCROLL (updateScrollbars calls SetScrollInfo on both) and
    // a non-degenerate client rect so the view arithmetic is the arithmetic that ships.
    HWND host = CreateWindowExW(0, kHostClass, L"", WS_POPUP, 0, 0, 1000, 700, nullptr, nullptr,
                                hInst, nullptr);
    check(host != nullptr, "created the hidden parent window");
    HWND edit = host ? CreateWindowExW(0, sentinelide::kD2DEditorClass, L"",
                                       WS_CHILD | WS_VSCROLL | WS_HSCROLL, 0, 0, 900, 600, host,
                                       (HMENU)(INT_PTR)kEditId, hInst, nullptr)
                     : nullptr;
    check(edit != nullptr, "created the Direct2D editor as a real WS_CHILD");
    if (!host || !edit) {
        printf("\nFAILED: could not create the windows - the dialect could not be exercised.\n");
        if (host) DestroyWindow(host);
        printf("\n%d passed, %d failed\n", gPass, gFail);
        return 1;
    }
    check(GetDlgCtrlID(edit) == kEditId, "the control reports the id the host dispatches on");

    // ---------------------------------------------------------------------------
    printf("\n1. the exact saveFile sequence round-trips examples/crypto.sentinel byte-for-byte\n");
    const std::string diskBytes = readFileBytes(goldenPath.c_str());
    check(!diskBytes.empty(), "read the committed signed golden file (GENERIC_READ)");
    if (diskBytes.empty()) {
        printf("     (no golden file at %ls)\n", goldenPath.c_str());
    } else {
        const std::wstring wide = utf8ToW(diskBytes);
        // WM_SETTEXT, via the same SetWindowTextW loadFileIntoEditor uses (:737).
        SetWindowTextW(edit, wide.c_str());

        LONG n = -1, got = -1;
        const std::wstring fetched = saveFileFetch(edit, n, got);
        printf("     disk %zu bytes / %zu CR / %zu LF; EM_GETTEXTLENGTHEX(GTL_NUMCHARS) = %ld;"
               " EM_GETTEXTEX(GT_USECRLF) = %ld\n",
               diskBytes.size(), countCh(diskBytes, '\r'), countCh(diskBytes, '\n'), n, got);

        check(n > 0, "GTL_NUMCHARS is positive for a non-empty document");
        check(got > 0, "GT_USECRLF returned characters");
        const size_t cr = countCh(fetched, L'\r'), lf = countCh(fetched, L'\n');
        printf("     fetched %zu chars / %zu CR / %zu LF\n", fetched.size(), cr, lf);
        check(cr == countCh(diskBytes, '\r'), "the CR count survived the round trip");
        check(cr > 0, "there were CRs to survive (the file really is CRLF)");
        check(cr == lf, "every break is CRLF - no lone CR, no lone LF");
        // n counts the INTERNAL form (one code unit per break); got counts the on-disk form
        // (two). The difference is exactly the number of breaks, which is what makes the
        // host's `n*2 + 16` buffer sizing sufficient rather than merely lucky.
        check(static_cast<size_t>(got) == static_cast<size_t>(n) + cr,
              "got == numchars + one extra unit per line break");
        check(fetched.size() == static_cast<size_t>(got), "the returned count is the string length");

        const std::string outBytes = wToUtf8(fetched);
        check(outBytes.size() == diskBytes.size(), "same byte length as the file on disk");
        check(outBytes == diskBytes, "BYTE-IDENTICAL to the committed signed file");
    }

    // ---------------------------------------------------------------------------
    printf("\n2. the editorText() sequence (GT_DEFAULT) is self-consistent and has no CR\n");
    {
        LONG n = -1, got = -1;
        const std::wstring s = editorTextFetch(edit, n, got);
        printf("     GTL_NUMCHARS = %ld, EM_GETTEXTEX(GT_DEFAULT) = %ld, string %zu\n", n, got,
               s.size());
        check(got == n, "GT_DEFAULT returns exactly the length GTL_NUMCHARS promised");
        check(s.size() == static_cast<size_t>(n), "and the buffer holds that many characters");
        check(s.find(L'\r') == std::wstring::npos, "the internal form contains no CR at all");
        // The two forms are never compared with each other (MainWindow.cpp:536-539); each
        // only has to be self-consistent, and in BOTH a line break is ONE character - which
        // is what keeps offsets in this index space aligned with EM_EXSETSEL's.
        check(countCh(s, L'\n') == static_cast<size_t>(SendMessageW(edit, EM_GETLINECOUNT, 0, 0)) - 1,
              "one LF per line break, consistent with EM_GETLINECOUNT");
    }

    // ---------------------------------------------------------------------------
    printf("\n3. EM_GETTEXTEX honours cb as BYTES (the heap-overrun hazard)\n");
    {
        // A canary past the declared capacity is the whole mechanism. cb = 32 bytes means
        // 16 wchar_t of room, so the handler may write indices 0..15 and NOTHING ELSE.
        //   * read cb as a CHARACTER count -> it writes 0..31 -> canary at 16 dies.
        //   * drop the `- 1` (no room reserved for the terminator) -> it writes 0..16 ->
        //     canary at 16 dies.
        // Either way editorText()'s exactly-n+1 buffer is overrun on every keystroke in the
        // shipping app, so this is the assertion standing between a typo and heap corruption.
        constexpr size_t kBufW = 256;
        constexpr wchar_t kCanary = static_cast<wchar_t>(0xCDCD);
        constexpr DWORD kShortCb = 32;
        constexpr size_t kCap = kShortCb / sizeof(wchar_t);  // 16

        const LONG full = lengthEx(edit, GTL_NUMCHARS);
        check(full > static_cast<LONG>(kCap), "the document is longer than the short buffer");

        std::vector<wchar_t> buf(kBufW, kCanary);
        GETTEXTEX gt{};
        gt.cb = kShortCb;
        gt.flags = GT_USECRLF;
        gt.codepage = 1200;
        const LONG got =
            static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf.data()));
        printf("     cb = %lu bytes (%zu wchar_t) -> returned %ld\n",
               static_cast<unsigned long>(kShortCb), kCap, got);
        check(got >= 0, "a short cb returns a count, not an error");
        check(got == static_cast<LONG>(kCap) - 1,
              "returned a TRUNCATED count of cap-1, not the whole document");
        // Bounds-checked before indexing: a handler that returned a negative or oversized
        // count is exactly the bug being hunted, and this test must FAIL on it, not fault.
        check(got >= 0 && got < static_cast<LONG>(kBufW) &&
                  buf[static_cast<size_t>(got)] == L'\0',
              "the returned count is in range and NUL-terminated");
        bool canaryIntact = true;
        size_t firstDead = 0;
        for (size_t i = kCap; i < kBufW; ++i) {
            if (buf[i] != kCanary) {
                canaryIntact = false;
                firstDead = i;
                break;
            }
        }
        if (!canaryIntact)
            printf("     OVERRUN: byte-capacity was %zu wchar_t but index %zu was written\n", kCap,
                   firstDead);
        check(canaryIntact, "nothing was written past cb / sizeof(wchar_t) - no overrun");

        // The truncation must be a PREFIX, not garbage.
        LONG fn = -1, fg = -1;
        const std::wstring whole = saveFileFetch(edit, fn, fg);
        check(got >= 0 && static_cast<size_t>(got) <= whole.size() &&
                  whole.compare(0, static_cast<size_t>(got), buf.data(),
                                static_cast<size_t>(got)) == 0,
              "the truncated text is a prefix of the full text");

        // cb = 0 must touch nothing at all: `if (cap == 0) return 0` before any write.
        std::fill(buf.begin(), buf.end(), kCanary);
        gt.cb = 0;
        const LONG zero =
            static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf.data()));
        check(zero == 0, "cb == 0 returns 0");
        check(buf[0] == kCanary, "cb == 0 wrote nothing, not even a terminator");

        // cb = one wchar_t: room for the terminator and nothing else.
        std::fill(buf.begin(), buf.end(), kCanary);
        gt.cb = static_cast<DWORD>(sizeof(wchar_t));
        const LONG one =
            static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf.data()));
        check(one == 0, "cb == sizeof(wchar_t) returns 0 characters");
        check(buf[0] == L'\0' && buf[1] == kCanary, "...and writes exactly the terminator");

        // An odd cb (not a whole number of code units) must round DOWN, never up.
        std::fill(buf.begin(), buf.end(), kCanary);
        gt.cb = kShortCb + 1;
        const LONG odd =
            static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf.data()));
        check(odd == static_cast<LONG>(kCap) - 1, "an odd cb rounds down to whole code units");
        check(buf[kCap] == kCanary, "...and still writes nothing past that");

        // GT_DEFAULT takes the identical cb path - both forks share the arithmetic.
        std::fill(buf.begin(), buf.end(), kCanary);
        gt.cb = kShortCb;
        gt.flags = GT_DEFAULT;
        const LONG def =
            static_cast<LONG>(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf.data()));
        check(def == static_cast<LONG>(kCap) - 1, "GT_DEFAULT truncates by cb the same way");
        check(buf[kCap] == kCanary, "...with the same capacity bound");

        // Null pointers must be refused, not dereferenced.
        check(SendMessageW(edit, EM_GETTEXTEX, 0, (LPARAM)buf.data()) == 0,
              "a null GETTEXTEX returns 0 rather than faulting");
        check(SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, 0) == 0,
              "a null output buffer returns 0 rather than faulting");
    }

    // ---------------------------------------------------------------------------
    printf("\n4. EM_GETTEXTLENGTHEX is never negative and never 0 for a non-empty document\n");
    {
        // BE LENIENT, NEVER FAIL CLOSED is the handler's stated contract, and both polarities
        // are lethal at the call sites: a negative return reaches `s.resize(n*2 + 16)` and
        // throws mid-save, a zero return makes saveFile write an EMPTY file over a committed
        // signed one. Unknown flags must therefore be ignored, never rejected.
        struct FlagCase {
            DWORD flags;
            const char* name;
        };
        const FlagCase cases[] = {
            {GTL_DEFAULT, "GTL_DEFAULT"},
            {GTL_NUMCHARS, "GTL_NUMCHARS"},
            {GTL_NUMBYTES, "GTL_NUMBYTES"},
            {GTL_PRECISE, "GTL_PRECISE (no NUMCHARS/NUMBYTES - an incomplete combination)"},
            {GTL_CLOSE, "GTL_CLOSE"},
            {GTL_USECRLF, "GTL_USECRLF"},
            {GTL_NUMCHARS | GTL_PRECISE, "GTL_NUMCHARS | GTL_PRECISE"},
            {0xFFFFFFFFu, "every bit set (unknown flags)"},
            {0x8000u, "an undefined flag on its own"},
        };
        bool allNonNegative = true, allNonZero = true;
        for (const auto& c : cases) {
            const LONG v = lengthEx(edit, c.flags);
            printf("     %-56s -> %ld\n", c.name, v);
            if (v < 0) allNonNegative = false;
            if (v <= 0) allNonZero = false;
        }
        check(allNonNegative, "no flag combination returns a negative length");
        check(allNonZero, "no flag combination returns 0 for a non-empty document");

        const LONG viaNull =
            static_cast<LONG>(SendMessageW(edit, EM_GETTEXTLENGTHEX, 0, 0));
        check(viaNull >= 0, "a null GETTEXTLENGTHEX returns a length, not an error");
        check(viaNull == lengthEx(edit, GTL_NUMCHARS), "...and defaults to GTL_NUMCHARS");

        // The one flag that must actually change the answer.
        check(lengthEx(edit, GTL_USECRLF) > lengthEx(edit, GTL_NUMCHARS),
              "GTL_USECRLF reports the larger on-disk length");
        check(lengthEx(edit, 0xFFFFFFFFu) == lengthEx(edit, GTL_USECRLF),
              "all-ones includes GTL_USECRLF and agrees with it");

        // WM_GETTEXTLENGTH, the plain-window form, must agree with the internal count.
        check(static_cast<LONG>(SendMessageW(edit, WM_GETTEXTLENGTH, 0, 0)) ==
                  lengthEx(edit, GTL_NUMCHARS),
              "WM_GETTEXTLENGTH agrees with GTL_NUMCHARS");
    }

    // ---------------------------------------------------------------------------
    printf("\n5. EM_LINEINDEX and EM_EXLINEFROMCHAR are inverses; past the end is -1\n");
    {
        const LONG total = static_cast<LONG>(SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
        const LONG len = lengthEx(edit, GTL_NUMCHARS);
        printf("     EM_GETLINECOUNT = %ld over %ld characters\n", total, len);
        check(total >= 1, "line count is at least 1");

        bool startsOk = true, inverseOk = true, ascending = true;
        LONG prev = -1;
        for (LONG i = 0; i < total; ++i) {
            const LONG idx = lineIndex(edit, i);
            if (idx < 0 || idx > len) startsOk = false;
            if (idx <= prev) ascending = false;
            prev = idx;
            if (lineFromChar(edit, idx) != i) inverseOk = false;
        }
        check(startsOk, "every line start is a valid offset");
        check(ascending, "line starts are strictly increasing");
        check(inverseOk, "EM_EXLINEFROMCHAR(EM_LINEINDEX(i)) == i for every line");

        // The other direction, over EVERY character offset: the line a character belongs to
        // must be the last line whose start is at or before it. MainWindow.cpp:1923-1925
        // computes Col = cpMin - lineStart + 1 from exactly this pair, and would print a
        // NEGATIVE column if it ever slipped.
        bool containment = true, noNegativeCol = true;
        for (LONG o = 0; o <= len; ++o) {
            const LONG ln = lineFromChar(edit, o);
            if (ln < 0 || ln >= total) {
                containment = false;
                break;
            }
            const LONG start = lineIndex(edit, ln);
            if (start > o) containment = false;
            if (o - start + 1 < 1) noNegativeCol = false;
            const LONG next = lineIndex(edit, ln + 1);
            if (next >= 0 && next <= o) containment = false;
        }
        check(containment, "every offset maps to the line that actually contains it");
        check(noNegativeCol, "Col = cpMin - lineStart + 1 is never below 1");

        check(lineIndex(edit, total) == -1, "EM_LINEINDEX one past the last line returns -1");
        check(lineIndex(edit, total + 50) == -1, "EM_LINEINDEX far past the end returns -1");
        // markErrorLines depends on BOTH halves: `if (start < 0) continue` and
        // `next < 0 ? -1 : next`. -1 must appear exactly once, at total, and never before.
        check(lineIndex(edit, total - 1) >= 0, "the LAST line still has a real start");
        // gotoLineCol passes `line - 1` unchecked, so a diagnostic reporting line 0 arrives
        // as (WPARAM)-1. RichEdit's documented meaning is "the caret's line".
        setSel(edit, 0, 0);
        check(lineIndex(edit, -1) == 0, "EM_LINEINDEX(-1) is the caret's line, not an error");

        check(lineFromChar(edit, len + 1000) == total - 1,
              "EM_EXLINEFROMCHAR past the end clamps to the last line");
        check(lineFromChar(edit, -1) >= 0, "EM_EXLINEFROMCHAR(-1) is the caret's line");
        check(static_cast<LONG>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0)) >= 0,
              "EM_GETFIRSTVISIBLELINE is never negative");
    }

    // ---------------------------------------------------------------------------
    printf("\n6. EM_EXSETSEL: cpMax == -1 selects to end, and out-of-range clamps\n");
    {
        const LONG len = lengthEx(edit, GTL_NUMCHARS);

        setSel(edit, 0, -1);
        CHARRANGE cr = getSel(edit);
        check(cr.cpMin == 0 && cr.cpMax == len, "{0,-1} selects the whole document");

        setSel(edit, 5, -1);
        cr = getSel(edit);
        check(cr.cpMin == 5 && cr.cpMax == len, "{5,-1} selects from 5 to the end");

        setSel(edit, len + 1000, len + 2000);
        cr = getSel(edit);
        check(cr.cpMin == len && cr.cpMax == len, "wholly out-of-range clamps to the end");

        setSel(edit, -100, 7);
        cr = getSel(edit);
        check(cr.cpMin == 0 && cr.cpMax == 7, "a negative cpMin clamps to 0");

        setSel(edit, 10, 4);
        cr = getSel(edit);
        check(cr.cpMin == 4 && cr.cpMax == 10, "a reversed range is reported normalised");

        // THE gotoLineCol SHAPE. It computes lineStart + (col - 1) with NO upper bound, so a
        // diagnostic whose column runs past the line end - or past the document - lands here.
        // It must clamp, never index out of range and never assert.
        const LONG ci = lineIndex(edit, 1) + 100000;
        setSel(edit, ci, ci);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        cr = getSel(edit);
        check(cr.cpMin == len && cr.cpMax == len,
              "gotoLineCol's unclamped column clamps to the document end");
        check(cr.cpMin >= 0 && cr.cpMax <= len, "the reported range is always inside the document");

        SendMessageW(edit, EM_EXSETSEL, 0, 0);  // null CHARRANGE
        check(true, "a null CHARRANGE is ignored rather than dereferenced");

        // The WM_SETREDRAW gate: a programmatic selection storm reports its NET effect once,
        // not once per step. Every colouring path in the host runs inside such a window.
        drainQueueUndispatched();
        gC = Counts{};
        setSel(edit, 0, 0);                              // settle, and let it report
        const int base = gC.selchange;
        SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        setSel(edit, 3, 9);
        setSel(edit, 11, 20);
        setSel(edit, 0, 0);                              // ...back where it started
        check(gC.selchange == base, "no EN_SELCHANGE escapes a WM_SETREDRAW(FALSE) window");
        SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
        check(gC.selchange == base, "a storm whose NET effect is nothing reports nothing");

        SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
        setSel(edit, 3, 9);
        SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
        check(gC.selchange == base + 1, "a storm with a NET effect reports exactly once");
        check(gC.selMin == 3 && gC.selMax == 9, "...carrying the settled, normalised range");
        check(gC.selTyp == SEL_TEXT, "...and SEL_TEXT for a non-empty selection");
    }

    // ---------------------------------------------------------------------------
    printf("\n7. EM_EXSETSEL clears the sticky vertical column (desiredX)\n");
    {
        // Three IDENTICAL lines, so "the same column" is the same x on every one of them
        // whatever font DirectWrite ends up choosing. Nothing below depends on a metric.
        const std::wstring line(40, L'x');
        const std::wstring doc = line + L"\n" + line + L"\n" + line;
        SetWindowTextW(edit, doc.c_str());
        const LONG l0 = lineIndex(edit, 0), l1 = lineIndex(edit, 1);
        check(l0 == 0 && l1 == 41, "the synthetic document indexed as expected");

        // Arm the sticky column at ~30, the way Up/Down does.
        setSel(edit, l0 + 30, l0 + 30);
        SendMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
        const LONG afterDown = sentinelide::d2dEditorCaretOffset(edit);
        printf("     Down from line 0 col 30 landed at line 1 col %ld\n", afterDown - l1);
        check(afterDown - l1 >= 28 && afterDown - l1 <= 32,
              "Down carried the column onto the next line (the sticky column works)");

        // Now jump the caret the way gotoLineCol does - EM_EXSETSEL, no keystroke - and go
        // down again. THE REGRESSION: before the fix, desiredX still held the old column and
        // the caret landed there instead of under the jump. Measured against RichEdit at the
        // time: RichEdit gave Ln 2 Col 3, this control gave Ln 2 Col 31.
        setSel(edit, l0 + 2, l0 + 2);
        SendMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
        const LONG afterJump = sentinelide::d2dEditorCaretOffset(edit);
        printf("     after EM_EXSETSEL to col 2, Down landed at line 1 col %ld"
               " (stale would be ~30)\n",
               afterJump - l1);
        check(afterJump - l1 >= 0 && afterJump - l1 <= 4,
              "Down after a programmatic jump uses the JUMPED-TO column, not the stale one");
    }

    // ---------------------------------------------------------------------------
    printf("\n8. EN_CHANGE IS SYNCHRONOUS (the check that catches a regression to Post)\n");
    {
        // No pump anywhere in this block. Each step drains the queue WITHOUT dispatching,
        // sends exactly one message, and then requires that the parent's counter has ALREADY
        // moved and that nothing was left queued. Under PostMessageW both halves fail.
        drainQueueUndispatched();
        gC = Counts{};
        const int before = gC.change;
        SendMessageW(edit, WM_CHAR, static_cast<WPARAM>(L'Z'), 1);
        const int afterChar = gC.change;  // read BEFORE anything else can pump
        check(afterChar == before + 1,
              "the parent's EN_CHANGE count moved before SendMessageW returned");
        check(!queueHasCommandFor(host), "nothing was POSTED to the parent - it was SENT");
        check(gC.change == before + 1, "exactly ONE EN_CHANGE per edit, not two");

        LONG n = -1, got = -1;
        const std::wstring afterText = editorTextFetch(edit, n, got);
        check(afterText.find(L'Z') != std::wstring::npos,
              "and the edit the notification announced is really in the buffer");

        // WM_SETTEXT raises it too - that is exactly why g.loadingFile exists, and the guard
        // only works because the notification lands INSIDE the straight line that sets it.
        drainQueueUndispatched();
        gC = Counts{};
        SetWindowTextW(edit, L"one\ntwo\nthree");
        check(gC.change == 1, "WM_SETTEXT raised exactly one synchronous EN_CHANGE");
        check(!queueHasCommandFor(host), "...and posted nothing");

        // EM_UNDO / EM_REDO are the ONLY undo path in the shipping exe (the accelerator table
        // claims Ctrl+Z/Ctrl+Y first), so an undo that moved the buffer without an EN_CHANGE
        // would leave g.dirty false and the buffer would be discarded with no prompt.
        drainQueueUndispatched();
        gC = Counts{};
        SendMessageW(edit, WM_CHAR, static_cast<WPARAM>(L'!'), 1);
        check(gC.change == 1, "a typed character notified");
        check(SendMessageW(edit, EM_CANUNDO, 0, 0) != 0, "EM_CANUNDO gates undo open");
        gC = Counts{};
        check(SendMessageW(edit, EM_UNDO, 0, 0) != 0, "EM_UNDO reports success");
        check(gC.change == 1, "EM_UNDO raised a synchronous EN_CHANGE");
        check(SendMessageW(edit, EM_CANREDO, 0, 0) != 0, "EM_CANREDO gates redo open");
        gC = Counts{};
        check(SendMessageW(edit, EM_REDO, 0, 0) != 0, "EM_REDO reports success");
        check(gC.change == 1, "EM_REDO raised a synchronous EN_CHANGE");
        check(!queueHasCommandFor(host), "no undo/redo notification was posted");

        // The deliberate divergences: formatting and selection change no text, so they must
        // raise NOTHING. A spurious EN_CHANGE from here could drop the host's g.highlighting
        // guard mid-highlight (a plain bool that clearErrorMarks/markErrorLines clear
        // unconditionally).
        gC = Counts{};
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = RGB(255, 0, 0);
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        check(gC.change == 0, "EM_SETCHARFORMAT/SCF_SELECTION raises no EN_CHANGE");
        setSel(edit, 1, 4);
        check(gC.change == 0, "EM_EXSETSEL raises no EN_CHANGE");
        check(gC.selchange >= 1, "...but it does raise EN_SELCHANGE");
    }

    // ---------------------------------------------------------------------------
    printf("\n9. the empty document still answers sanely\n");
    {
        drainQueueUndispatched();
        SetWindowTextW(edit, L"");
        check(lengthEx(edit, GTL_NUMCHARS) == 0, "an EMPTY document reports 0 - and only then");
        check(lengthEx(edit, GTL_USECRLF) == 0, "...on the CRLF fork too");
        check(static_cast<LONG>(SendMessageW(edit, EM_GETLINECOUNT, 0, 0)) == 1,
              "an empty document is one line, never zero");
        check(lineIndex(edit, 0) == 0, "line 0 starts at 0");
        check(lineIndex(edit, 1) == -1, "line 1 does not exist");
        LONG n = -1, got = -1;
        const std::wstring s = saveFileFetch(edit, n, got);
        check(got == 0 && s.empty(), "saveFile's sequence yields an empty string, not garbage");

        // And one character is not zero: the "writes an empty file" hazard, from the other
        // side. saveFile refuses to write when n > 0 and got <= 0, so this pair is what
        // decides whether a one-character file can be saved at all.
        SendMessageW(edit, WM_CHAR, static_cast<WPARAM>(L'a'), 1);
        check(lengthEx(edit, GTL_NUMCHARS) == 1, "a one-character document reports 1");
        const std::wstring one = saveFileFetch(edit, n, got);
        check(got == 1 && one == L"a", "...and fetches back as exactly that character");
    }

    // ---------------------------------------------------------------------------
    printf("\n10. UNDO GRANULARITY survives slice 4's colouring and error tints\n");
    {
        // THE regression this exists to catch, and the reason the whole slice is shaped the
        // way it is. RichEdit is coloured by SELECTING each token and setting a character
        // format — ~500 EM_EXSETSELs per keystroke — and the error tints do the same thing
        // once per diagnostic. On this control every one of those reaches
        // EditorModel::setSelection, which clears typingRun_, and insertText only coalesces
        // while that is still set. So a colouring scheme built on the selection would turn a
        // typed word into one undo step PER CHARACTER, each pushing a whole-document
        // snapshot, and with kMaxUndo = 200 the history would collapse to the last 200
        // characters typed. Slice 4 therefore colours by PAINTING and tints by PAINTING.
        //
        // This measures the actual number rather than asserting the design.
        const int kChars = 60;
        SetWindowTextW(edit, L"");  // also clears the undo stack, as WM_SETTEXT must
        for (int i = 0; i < kChars; ++i) {
            SendMessageW(edit, WM_CHAR, static_cast<WPARAM>(L'a' + (i % 26)), 1);
            // Every keystroke in the real app is followed by a repaint, and the repaint is
            // where the colouring runs. Skipping it would measure a path nobody executes.
            SendMessageW(edit, WM_PAINT, 0, 0);
        }
        // ...and a build lands in the middle of it. This is markErrorLines' whole
        // implementation on this control; if it moved the selection, the run would break
        // here and the count below would be 2, not 1.
        std::vector<int> errLines;
        errLines.push_back(0);
        sentinelide::d2dEditorSetErrorLines(edit, errLines);
        SendMessageW(edit, WM_PAINT, 0, 0);
        for (int i = 0; i < 5; ++i) {
            SendMessageW(edit, WM_CHAR, static_cast<WPARAM>(L'z'), 1);
            SendMessageW(edit, WM_PAINT, 0, 0);
        }
        sentinelide::d2dEditorSetErrorLines(edit, std::vector<int>());

        check(lengthEx(edit, GTL_NUMCHARS) == kChars + 5, "65 characters went in");
        int steps = 0;
        while (SendMessageW(edit, EM_CANUNDO, 0, 0) != 0 && steps < 200) {
            SendMessageW(edit, EM_UNDO, 0, 0);
            ++steps;
        }
        printf("     65 characters typed (with a repaint after each, and an error-tint set\n"
               "     and cleared partway) = %d undo step(s)\n", steps);
        check(steps == 1, "ONE undo step for the whole run - colouring did not break it");
        check(lengthEx(edit, GTL_NUMCHARS) == 0, "and that one undo emptied the document");

        // The other half of the claim, so the number above means something: do the SAME
        // thing with a selection move between keystrokes — which is exactly what
        // applyColor/applyBackColor do — and watch it fall apart. This is why highlight(),
        // clearErrorMarks and markErrorLines are gated off this control rather than being
        // left to be "a no-op that costs nothing".
        SetWindowTextW(edit, L"");
        for (int i = 0; i < kChars; ++i) {
            SendMessageW(edit, WM_CHAR, static_cast<WPARAM>(L'a' + (i % 26)), 1);
            setSel(edit, 0, 1);  // stand-in for applyColor's EM_EXSETSEL
        }
        int stormSteps = 0;
        while (SendMessageW(edit, EM_CANUNDO, 0, 0) != 0 && stormSteps < 200) {
            SendMessageW(edit, EM_UNDO, 0, 0);
            ++stormSteps;
        }
        printf("     the same 60 characters WITH an EM_EXSETSEL between each = %d undo step(s)\n",
               stormSteps);
        check(stormSteps >= 10, "a selection-based highlighter really would shatter the run");
    }

    // ---------------------------------------------------------------------------
    // The control PAINTS its caret, so there is no GDI caret to see. That is a rendering
    // choice; having no SYSTEM caret was an accessibility REGRESSION against RichEdit,
    // because GetGUIThreadInfo/OBJID_CARET is the only thing Narrator, Magnifier's
    // "follow the text cursor" and IME candidate placement can follow. Slice 5 creates a
    // real caret (never shown) and keeps it on the painted one; this is what says so.
    //
    // Nothing here is a font or pixel metric: every expected position is asked of the
    // control through EM_POSFROMCHAR, which returns drawContent's own origin. The claim
    // under test is therefore an invariant — "the system caret is exactly where the control
    // says that character is" — and it holds at any font, DPI or theme.
    printf("\n11. a real SYSTEM caret exists, tracks the painted caret, and dies with focus\n");
    {
        auto caretOwner = []() -> HWND {
            GUITHREADINFO gi{};
            gi.cbSize = sizeof(gi);
            return GetGUIThreadInfo(GetCurrentThreadId(), &gi) ? gi.hwndCaret : nullptr;
        };
        auto caretRect = []() -> RECT {
            GUITHREADINFO gi{};
            gi.cbSize = sizeof(gi);
            if (!GetGUIThreadInfo(GetCurrentThreadId(), &gi)) return RECT{ 0, 0, 0, 0 };
            return gi.rcCaret;
        };
        auto posFromChar = [](HWND e, LONG off) -> POINTL {
            POINTL p{ -9999, -9999 };
            SendMessageW(e, EM_POSFROMCHAR, (WPARAM)&p, static_cast<LPARAM>(off));
            return p;
        };

        SendMessageW(edit, WM_KILLFOCUS, 0, 0);
        check(caretOwner() != edit, "with no focus, the editor owns no caret");

        // A document tall and wide enough that the caret can be moved and scrolled away
        // from the origin in both axes.
        std::wstring doc;
        for (int i = 0; i < 200; ++i) doc += L"let alpha = beta + gamma * delta;  // a line\n";
        SetWindowTextW(edit, doc.c_str());
        SetFocus(edit);
        check(GetFocus() == edit, "the editor took the focus");
        check(caretOwner() == edit, "...and CREATED a system caret that it owns");

        // At three caret positions: the top of the document, a column well into line 5, and
        // line 150 — which is far enough down that ensureCaretVisible must scroll to reach
        // it, so the y below is only right if the caret follows the VIEW as well as the
        // offset.
        struct { LONG line, col; } spots[] = { { 0, 0 }, { 5, 20 }, { 150, 12 } };
        for (auto& s : spots) {
            const LONG off = lineIndex(edit, s.line) + s.col;
            setSel(edit, off, off);
            SendMessageW(edit, EM_SCROLLCARET, 0, 0);
            SendMessageW(edit, WM_PAINT, 0, 0);
            const POINTL want = posFromChar(edit, off);
            POINT got{ -1, -1 };
            GetCaretPos(&got);
            printf("     line %3ld col %2ld: EM_POSFROMCHAR (%ld,%ld), GetCaretPos (%ld,%ld)\n",
                   s.line, s.col, want.x, want.y, got.x, got.y);
            check(got.x == want.x && got.y == want.y,
                  "the system caret sits exactly where the control puts that character");
        }

        // The caret must follow a scroll that does NOT move it in the document — this is
        // the case a "set it when the selection changes" implementation gets wrong.
        const LONG off = lineIndex(edit, 150) + 12;
        SendMessageW(edit, WM_PAINT, 0, 0);
        POINT before{ 0, 0 };
        GetCaretPos(&before);
        for (int i = 0; i < 3; ++i)
            SendMessageW(edit, WM_VSCROLL, MAKEWPARAM(SB_LINEUP, 0), 0);
        SendMessageW(edit, WM_PAINT, 0, 0);
        POINT after{ 0, 0 };
        GetCaretPos(&after);
        const POINTL wantAfter = posFromChar(edit, off);
        printf("     after 3x WM_VSCROLL(SB_LINEUP): y %ld -> %ld, EM_POSFROMCHAR y %ld\n",
               before.y, after.y, wantAfter.y);
        check(after.y != before.y, "a pure scroll moved the system caret");
        check(after.x == wantAfter.x && after.y == wantAfter.y,
              "...and left it exactly on EM_POSFROMCHAR");

        // Its HEIGHT is the line pitch, taken from the control the same way: two adjacent
        // lines' y difference. A one-pixel caret would report an insertion point with no
        // extent for a magnifier to zoom to. Within ONE pixel, not exactly: the pitch is a
        // float (25.67px here) and both numbers truncate it — the caret once, the two
        // EM_POSFROMCHAR ys at different fractional offsets — so adjacent lines can measure
        // 26 apart while the caret is 25 tall. Anything larger than that is a real defect.
        const POINTL y0 = posFromChar(edit, lineIndex(edit, 150));
        const POINTL y1 = posFromChar(edit, lineIndex(edit, 151));
        const RECT rc = caretRect();
        const LONG pitch = y1.y - y0.y, tall = rc.bottom - rc.top;
        printf("     line pitch %ld, GUITHREADINFO rcCaret height %ld\n", pitch, tall);
        check(tall > 1 && (tall - pitch <= 1) && (pitch - tall <= 1),
              "the caret is one line tall (to the pixel a float pitch rounds to)");
        check(rc.left == after.x && rc.top == after.y,
              "GUITHREADINFO reports the same point (this is what an AT actually reads)");

        SendMessageW(edit, WM_KILLFOCUS, 0, 0);
        check(caretOwner() != edit, "kill-focus destroyed it - a caret is a per-THREAD object");
    }

    DestroyWindow(edit);
    DestroyWindow(host);

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
