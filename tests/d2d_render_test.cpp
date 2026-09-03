// SPDX-License-Identifier: GPL-3.0-or-later
// d2d_render_test — the pixel test for the Direct2D editor control (phase 46 slice 2).
//
// Build+run:  cmake --build build --target d2d_render_test && build\d2d_render_test.exe
// or via ctest:  ctest --test-dir build -R d2d_render
//
// WHY THIS TEST EXISTS. Before it, the control's rendering had zero automated coverage: a
// blank editor would have compiled, run, opened a window and passed every test in the repo.
// A control destined to replace the editor in a SHIPPING IDE cannot rely on someone
// remembering to look at a screenshot, and slices 4-7 (syntax colouring) are entirely about
// pixels.
//
// scripts\capture.ps1 does capture this control correctly, so it is a fine way to LOOK at
// it -- PROVIDED the window is FOREGROUND. It is not a way to TEST it: it needs a visible
// window someone has brought to the front, and a judgement call. This runs headless, is
// deterministic, and fails loudly. (An earlier note here claimed PrintWindow could not
// capture Direct2D at all. That was wrong and is retracted -- re-measured, a magenta-cleared
// D2D window captures as magenta and the demo captures with its real text. The real
// condition is compositing: a background or minimised D2D window has nothing to copy and
// captures blank EVERY time, and a background process cannot foreground it, so an automated
// run cannot use capture.ps1 on this control at all.)
//
// TWO ASSERTIONS ARE THE POINT OF THE WHOLE FILE. Case 4: a meaningful number of pixels
// must DIFFER from the background — a control that renders a blank window is the failure
// mode nothing else here can see, and the one an "it compiles and runs" check waves
// straight through. Case 5 (phase 46 slice 4, the slice this file was built for): all FIVE
// Theme text colours must be on screen, on the right LINES. A painter that regressed to
// drawing every run in one brush still passes case 4 and fails case 5, which is exactly
// the split that was wanted.
//
// Case 7 covers the error-line tints, the other half of slice 4 — painted decoration
// driven off g.problems, rather than RichEdit's select-and-set-a-background, whose real
// cost was undo granularity around every build.
//
// DELIBERATELY NOT BRITTLE. It asserts sizes, colour ranges and COUNTS, never glyph
// positions or exact pixel values, so a different font, a different DPI or a theme flip
// does not break it. Every colour comes from Theme.h at runtime, so the test follows the
// same light/dark decision the renderer made, and the line BANDS are asked of the control
// through EM_LINEINDEX + EM_POSFROMCHAR rather than computed here.
//
// SKIP, LOUDLY, is reserved for one thing: an environment that cannot create a D2D/WIC
// render target at all (case 0 probes for exactly that and returns 0 with a printed
// reason). A target that IS created and then yields a blank image is a FAILURE, not a skip.
//
// READ-ONLY on examples/: the input is opened GENERIC_READ and there is no write path to
// it. crypto.sentinel is a committed SIGNED file (see editor_model_test case 10).
#include <windows.h>

#include <wincodec.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "host/win32/D2DEditor.h"
#include "host/win32/D2DSupport.h"
#include "host/win32/Theme.h"

namespace {

int gPass = 0, gFail = 0;
void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

// The window is created at this size and never shown; the assertions read the CLIENT rect
// back rather than assuming it, because WS_HSCROLL|WS_VSCROLL eat some of it.
constexpr int kWinW = 1100;
constexpr int kWinH = 760;

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
    const int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), w.data(), n);
    return w;
}

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

// ---- decoded image ----------------------------------------------------------

struct Rgb {
    int r = 0, g = 0, b = 0;
};

struct Image {
    UINT w = 0, h = 0;
    std::vector<BYTE> px;  // BGRA, tightly packed, stride == w * 4
    Rgb at(UINT x, UINT y) const {
        const size_t i = (static_cast<size_t>(y) * w + x) * 4;
        return Rgb{px[i + 2], px[i + 1], px[i + 0]};
    }
};

// Distance in the worst channel — a plain, explainable metric. Everything below asserts
// against a threshold on this rather than on equality, because antialiasing means almost
// no pixel is exactly either colour.
int chanDist(const Rgb& a, const Rgb& b) {
    const int dr = abs(a.r - b.r), dg = abs(a.g - b.g), db = abs(a.b - b.b);
    return (dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db));
}

// Squared Euclidean distance, so nothing here needs <cmath> or a float.
int dist2(const Rgb& a, const Rgb& b) {
    const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

Rgb fromColorRef(COLORREF c) {
    return Rgb{GetRValue(c), GetGValue(c), GetBValue(c)};
}

// ---- the syntax palette, and reading a class back off the screen (slice 4) ---
// The five colours the editor can draw text in. Order is fixed; kPalNames matches.
enum PalIdx { kPalDefault = 0, kPalComment, kPalString, kPalNumber, kPalKeyword, kPalCount };
const char* const kPalNames[kPalCount] = {"textPrimary", "synComment", "synString",
                                          "synNumber", "synKeyword"};

// Which colour a pixel was DRAWN in, or -1 for "background, or an antialiased edge too
// faint to attribute". A pixel is attributed only when it is within 12 units of one of the
// five, which for grayscale-antialiased text means the solid interior of a glyph.
//
// WHY A TOLERANCE AND NOT EQUALITY: the offscreen path draws with GRAYSCALE antialiasing,
// so a glyph is a gradient from the background to its colour and only its core reaches the
// exact value. WHY 12: the five colours are at least 80 units apart from each other in
// both themes, so 12 cannot confuse two of them.
//
// ONE HONEST LIMITATION, stated because the assertions below are shaped around it. In the
// dark theme synComment IS very nearly a 70%-covered synNumber over the background
// (0.7 * (159,209,154) + 0.3 * (22,22,24) = (118,153,115) against synComment's
// (118,150,118)), so a partly-covered DIGIT can be attributed to synComment. It is never
// the other way round — a comment pixel can never be as bright as a number — so every
// assertion here either counts a class's PRESENCE, or asserts an absence only on a line
// that contains no numbers at all.
int classifyPixel(const Rgb& p, const Rgb* pal, const Rgb& bg) {
    if (chanDist(p, bg) <= 8) return -1;  // background, or all but indistinguishable from it
    int best = -1, bestD = 0;
    for (int i = 0; i < kPalCount; ++i) {
        const int d = dist2(p, pal[i]);
        if (d <= 12 * 12 && (best < 0 || d < bestD)) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

// Attribute every pixel in rows [y0, y1) and tally by class.
void countClasses(const Image& im, const Rgb* pal, const Rgb& bg, UINT y0, UINT y1,
                  long long out[kPalCount]) {
    for (int i = 0; i < kPalCount; ++i) out[i] = 0;
    if (y1 > im.h) y1 = im.h;
    for (UINT y = y0; y < y1; ++y) {
        for (UINT x = 0; x < im.w; ++x) {
            const int c = classifyPixel(im.at(x, y), pal, bg);
            if (c >= 0) ++out[c];
        }
    }
}

void printClasses(const char* label, const long long c[kPalCount]) {
    printf("     %-22s", label);
    for (int i = 0; i < kPalCount; ++i) printf("  %s=%lld", kPalNames[i], c[i]);
    printf("\n");
}

// The top of a line, in client pixels, ASKED OF THE CONTROL rather than computed here.
// EM_LINEINDEX + EM_POSFROMCHAR is the same pair the line-number gutter uses, so a font,
// DPI or theme change moves the band with the text instead of breaking the test. (Both
// message numbers come from winuser.h — see the include-order trap at the top of
// D2DEditor.cpp — which is what the control compiles them as too.)
LONG lineTopY(HWND hwnd, int line) {
    const LONG idx = static_cast<LONG>(SendMessageW(hwnd, EM_LINEINDEX,
                                                    static_cast<WPARAM>(line), 0));
    POINTL pt{};
    SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt),
                 static_cast<LPARAM>(idx < 0 ? 0 : idx));
    return pt.y;
}

// Decode the PNG back off disk. Converting to a fixed 32bppBGRA means the reader never has
// to care what the encoder negotiated.
bool loadPng(IWICImagingFactory* wic, const wchar_t* path, Image& out, const char*& why) {
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    why = "";
    if (FAILED(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &dec)) ||
        !dec) {
        why = "CreateDecoderFromFilename failed (no PNG written?)";
    } else if (FAILED(dec->GetFrame(0, &frame)) || !frame) {
        why = "GetFrame(0) failed";
    } else if (FAILED(frame->GetSize(&out.w, &out.h)) || out.w == 0 || out.h == 0) {
        why = "GetSize failed or zero-sized";
    } else if (FAILED(wic->CreateFormatConverter(&conv)) || !conv ||
               FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom))) {
        why = "CreateFormatConverter/Initialize failed";
    } else {
        const UINT stride = out.w * 4;
        out.px.resize(static_cast<size_t>(stride) * out.h);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, stride,
                                        static_cast<UINT>(out.px.size()), out.px.data()));
        if (!ok) why = "CopyPixels failed";
    }
    sentinelide::SafeRelease(conv);
    sentinelide::SafeRelease(frame);
    sentinelide::SafeRelease(dec);
    return ok;
}

// Can this machine make a D2D render target over a WIC bitmap AT ALL? If not, the control
// is untestable here and the honest answer is a loud skip — not a red test that says the
// editor is broken when it is the environment that is.
bool probeD2DWic(IWICImagingFactory* wic, const char*& why) {
    why = "";
    if (!sentinelide::d2dFactory()) {
        why = "D2D1CreateFactory returned nothing";
        return false;
    }
    if (!sentinelide::dwriteFactory()) {
        why = "DWriteCreateFactory returned nothing";
        return false;
    }
    IWICBitmap* bmp = nullptr;
    ID2D1RenderTarget* rt = nullptr;
    bool ok = false;
    if (FAILED(wic->CreateBitmap(8, 8, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 &bmp)) ||
        !bmp) {
        why = "IWICImagingFactory::CreateBitmap failed";
    } else {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
            96.0f);
        ok = SUCCEEDED(sentinelide::d2dFactory()->CreateWicBitmapRenderTarget(bmp, props, &rt)) &&
             rt != nullptr;
        if (!ok) why = "ID2D1Factory::CreateWicBitmapRenderTarget failed";
    }
    sentinelide::SafeRelease(rt);
    sentinelide::SafeRelease(bmp);
    return ok;
}

// Drive one real WM_PAINT through the control's window procedure and report whether it took
// the "device resources are good" branch.
//
// HOW THAT IS OBSERVABLE FROM OUTSIDE. paint() calls updateScrollbars ONLY inside that
// branch, so clearing the vertical range first and finding it re-established afterwards
// means the window's own ID2D1HwndRenderTarget was created (or survived) and the whole paint
// body ran. That is the check that the offscreen render did not wreck the live device: if
// d2dEditorRenderToPng had left st->rt aimed at the released WIC target, this would not
// return false, it would take the process down inside BeginDraw.
//
// The window is HIDDEN, so WM_PAINT is sent directly — UpdateWindow does nothing for an
// invisible window. BeginPaint/EndPaint are fine with that and paint() ignores ps.rcPaint.
bool livePaint(HWND hwnd) {
    SCROLLINFO clear{};
    clear.cbSize = sizeof(clear);
    clear.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    SetScrollInfo(hwnd, SB_VERT, &clear, FALSE);
    SendMessageW(hwnd, WM_PAINT, 0, 0);
    SCROLLINFO got{};
    got.cbSize = sizeof(got);
    got.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, SB_VERT, &got)) return false;
    return got.nMax > 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Paths come from the build (like editor_model_test's golden file) so the test does not
    // depend on a working directory.
    const std::wstring srcPath =
        (argc > 1) ? toW(argv[1]) : std::wstring(L"G:\\SentinelIDE\\examples\\crypto.sentinel");
    const std::wstring pngPath =
        (argc > 2) ? toW(argv[2]) : std::wstring(L"G:\\SentinelIDE\\build\\d2d_render.png");
    // Case 7's error-tint render goes to its own file so the one named above is left in the
    // plain state at the end — it is the image a human is meant to open and look at.
    const std::wstring tintPath = [&] {
        const size_t dot = pngPath.rfind(L'.');
        return (dot == std::wstring::npos ? pngPath : pngPath.substr(0, dot)) + L"_tint.png";
    }();

    printf("D2DEditor offscreen render\n\n");

    // Match the demo host: no .rc, so per-monitor-v2 has to be asserted in code or
    // GetDpiForWindow reports a lie and every metric below is scaled wrong.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        printf("SKIP: CoInitializeEx failed - no COM apartment, WIC is unreachable.\n");
        return 0;
    }

    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic))) ||
        !wic) {
        printf("SKIP: no WIC imaging factory on this machine - cannot render or decode.\n");
        CoUninitialize();
        return 0;
    }

    printf("0. the environment can host a D2D render target over a WIC bitmap\n");
    {
        const char* why = "";
        if (!probeD2DWic(wic, why)) {
            printf("  [SKIP] %s\n", why);
            printf("\nSKIPPED: this environment cannot create a D2D/WIC render target, so the\n"
                   "         control is untestable here. This is NOT a pass - re-run somewhere\n"
                   "         with Direct2D available before trusting the renderer.\n");
            sentinelide::SafeRelease(wic);
            CoUninitialize();
            return 0;
        }
        check(true, "D2D + DWrite factories and a WIC bitmap render target");
    }

    const sentinelide::Theme& th = sentinelide::currentTheme();
    const Rgb bg = fromColorRef(th.windowBg);
    const Rgb fg = fromColorRef(th.textPrimary);
    printf("     theme=%s  windowBg=(%d,%d,%d)  textPrimary=(%d,%d,%d)\n",
           th.dark ? "dark" : "light", bg.r, bg.g, bg.b, fg.r, fg.g, fg.b);
    // Straight from Theme.h at runtime, exactly as the renderer reads them, so the test
    // follows the same light/dark decision the control made instead of hard-coding one.
    const Rgb pal[kPalCount] = {fg, fromColorRef(th.synComment), fromColorRef(th.synString),
                                fromColorRef(th.synNumber), fromColorRef(th.synKeyword)};
    for (int i = 0; i < kPalCount; ++i)
        printf("     %-12s (%3d,%3d,%3d)\n", kPalNames[i], pal[i].r, pal[i].g, pal[i].b);

    printf("\n1. a hidden editor window renders examples/crypto.sentinel to a PNG\n");
    const std::string bytes = readFileBytes(srcPath.c_str());
    check(!bytes.empty(), "read the source file");

    HWND hwnd = nullptr;
    LONG clientW = 0, clientH = 0;
    bool rendered = false;
    {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        check(sentinelide::registerD2DEditorClass(hInst), "registered SentinelD2DEditor");
        // A hidden top-level popup, NOT shown: the render path draws into a WIC bitmap, so
        // nothing here needs the window on screen. That is the point -- the test runs with
        // no desktop, no compositor and no human.
        // WS_HSCROLL|WS_VSCROLL because the control drives both bars itself and relies on
        // them existing (SIF_DISABLENOSCROLL).
        hwnd = CreateWindowExW(0, sentinelide::kD2DEditorClass, L"", WS_POPUP | WS_HSCROLL | WS_VSCROLL,
                               0, 0, kWinW, kWinH, nullptr, nullptr, hInst, nullptr);
        check(hwnd != nullptr, "created a hidden editor window");
        if (hwnd) {
            sentinelide::d2dEditorSetText(hwnd, utf8ToW(bytes));
            RECT rc{};
            GetClientRect(hwnd, &rc);
            clientW = rc.right - rc.left;
            clientH = rc.bottom - rc.top;
            // A LIVE PAINT FIRST, and it is not decoration. d2dEditorRenderToPng parks the
            // window's target and brushes and puts them back; if the control has never
            // painted, that whole set is null and the swap proves nothing. Forcing a real
            // WM_PAINT here creates the ID2D1HwndRenderTarget and its brushes, so case 5's
            // repeat is testing the actual restore.
            check(livePaint(hwnd), "a real WM_PAINT built the window's own D2D device");
            rendered = sentinelide::d2dEditorRenderToPng(hwnd, pngPath.c_str());
            check(rendered, "d2dEditorRenderToPng returned true");
        }
    }

    Image im;
    bool decoded = false;
    if (rendered) {
        printf("\n2. the PNG decodes and is the control's client size\n");
        const char* why = "";
        decoded = loadPng(wic, pngPath.c_str(), im, why);
        if (!decoded) printf("     decode failure: %s\n", why);
        check(decoded, "decoded the PNG back off disk");
        if (decoded) {
            printf("     image %ux%u, client %ldx%ld\n", im.w, im.h, clientW, clientH);
            check(im.w == static_cast<UINT>(clientW) && im.h == static_cast<UINT>(clientH),
                  "image size == client size");
        }
    }

    if (decoded && im.w > 40 && im.h > 40) {
        // Sample points chosen for ROBUSTNESS, not precision: all of them sit below and to
        // the right of anything ~12 short lines of source could occupy, at any font or DPI
        // this control plausibly runs at. Nothing here depends on where a glyph landed.
        printf("\n3. the background is Theme windowBg well away from the text\n");
        {
            const UINT pts[][2] = {{im.w - 4, im.h - 4},         {im.w / 2, im.h - 4},
                                   {4, im.h - 4},                {im.w - 4, im.h * 7 / 8},
                                   {im.w / 2, im.h * 3 / 4},     {im.w - 4, im.h * 3 / 4}};
            int worst = 0;
            for (const auto& p : pts) {
                const int d = chanDist(im.at(p[0], p[1]), bg);
                if (d > worst) worst = d;
            }
            printf("     worst channel deviation across 6 sample points: %d\n", worst);
            check(worst <= 2, "every background sample is Theme windowBg");
        }

        printf("\n4. TEXT WAS ACTUALLY DRAWN (the assertion this file exists for)\n");
        {
            long long nonBg = 0;
            int bestFg = 255;  // closest any pixel gets to Theme textPrimary
            for (UINT y = 0; y < im.h; ++y) {
                for (UINT x = 0; x < im.w; ++x) {
                    const Rgb p = im.at(x, y);
                    if (chanDist(p, bg) > 8) ++nonBg;
                    const int d = chanDist(p, fg);
                    if (d < bestFg) bestFg = d;
                }
            }
            const long long total = static_cast<long long>(im.w) * im.h;
            printf("     %lld of %lld pixels differ from the background (%.3f%%)\n", nonBg, total,
                   100.0 * static_cast<double>(nonBg) / static_cast<double>(total));
            printf("     closest pixel to Theme textPrimary: %d channel-units away\n", bestFg);
            // ~11 lines of source at 11pt is thousands of inked pixels; 500 is a floor low
            // enough to survive a font or DPI change and high enough that a blank window,
            // or a stray artefact, cannot clear it.
            check(nonBg >= 500, "hundreds of non-background pixels -> the text really rendered");
            // A solid fill in the wrong colour would also beat the floor above; this catches
            // that by requiring the image to still be MOSTLY background.
            check(nonBg < total / 2, "the image is still mostly background (not a solid fill)");
            // <=48 is roughly ">=77% glyph coverage", i.e. at least one pixel is essentially
            // pure text colour rather than a faint antialiased edge.
            check(bestFg <= 48, "at least one pixel is close to Theme textPrimary");
        }

        printf("\n5. SYNTAX COLOURING — THE ASSERTION SLICE 4 EXISTS FOR\n");
        {
            // This is the case that must FAIL if the painter ever regresses to drawing
            // every run in one brush. It asserts PRESENCE and COUNTS of colours, never a
            // glyph position, so a font, DPI or theme change moves the pixels around
            // underneath it without breaking it.
            //
            // examples/crypto.sentinel is chosen input, not arbitrary input: it has two
            // full-line comments at the top, a comment mid-file, keywords on nearly every
            // line (fn / let / secret / u8 / i64 / if / else), two char literals and three
            // numeric literals. All five classes are therefore on screen at once, which is
            // the only way "at least N distinct text colours" means anything.
            long long all[kPalCount];
            countClasses(im, pal, bg, 0, im.h, all);
            printClasses("whole image", all);
            // A floor with better than 3x margin at the smallest class (synString, three
            // glyphs' worth) even if the same file is rendered at 96 dpi instead of this
            // machine's. Low enough to survive a font change, far too high for stray
            // antialiasing to reach.
            const long long kClassFloor = 25;
            int classesPresent = 0;
            for (int i = 0; i < kPalCount; ++i)
                if (all[i] >= kClassFloor) ++classesPresent;
            check(classesPresent == kPalCount,
                  "all FIVE Theme text colours are present -> the editor is not monochrome");
            check(all[kPalDefault] >= kClassFloor, "ordinary text in Theme textPrimary");
            check(all[kPalComment] >= kClassFloor, "comments in Theme synComment");
            check(all[kPalString] >= kClassFloor, "literals in Theme synString");
            check(all[kPalNumber] >= kClassFloor, "numbers in Theme synNumber");
            check(all[kPalKeyword] >= kClassFloor, "keywords in Theme synKeyword");

            // ...and the colours are on the RIGHT lines, which is what separates real
            // colouring from a painter that has simply been handed the wrong spans. The
            // bands come from EM_LINEINDEX + EM_POSFROMCHAR, i.e. the control is asked
            // where its own lines are.
            const LONG y0 = lineTopY(hwnd, 0);
            const LONG y1 = lineTopY(hwnd, 1);
            const LONG lineH = y1 - y0;
            printf("     line 0 top y=%ld, line pitch=%ld px\n", y0, lineH);
            check(lineH > 4 && y0 >= 0, "the control reports a sane line pitch");
            if (lineH > 4 && y0 >= 0) {
                const auto band = [&](int line, long long out[kPalCount]) {
                    const LONG top = lineTopY(hwnd, line);
                    countClasses(im, pal, bg, static_cast<UINT>(top < 0 ? 0 : top),
                                 static_cast<UINT>(top + lineH), out);
                };
                long long c0[kPalCount], c2[kPalCount], c3[kPalCount], c10[kPalCount];
                band(0, c0);    // "// crypto.sentinel — ..."        (a whole-line comment)
                band(2, c2);    // "fn main() -> i64 {"              (keywords, no comment)
                band(3, c3);    // "    let k: secret u8 = 'k';"     (a char literal)
                band(10, c10);  // "    if eq == i64_to_u8(0) { 42 } else { 0 }"  (numbers)
                printClasses("line 0  (comment)", c0);
                printClasses("line 2  (keywords)", c2);
                printClasses("line 3  (literal)", c3);
                printClasses("line 10 (numbers)", c10);
                check(c0[kPalComment] >= 100, "line 0 is painted in synComment");
                check(c0[kPalKeyword] == 0 && c0[kPalDefault] == 0,
                      "and ONLY in synComment - 'fn'/'let' inside a comment are not keywords");
                check(c2[kPalKeyword] >= 40, "line 2 has synKeyword pixels (fn, i64)");
                check(c2[kPalComment] == 0, "and no comment colour anywhere on it");
                check(c2[kPalDefault] >= 40, "with 'main' still in the ordinary text colour");
                check(c3[kPalString] >= 12, "line 3 has synString pixels ('k')");
                check(c10[kPalNumber] >= 20, "line 10 has synNumber pixels (0, 42, 0)");
                check(c10[kPalKeyword] >= 20, "and synKeyword on the same line (if, else)");
            }
        }

        printf("\n6. the LIVE window still paints after the offscreen render\n");
        {
            // THE regression this guards: d2dEditorRenderToPng parks the window's target and
            // brushes, points EditorState at a WIC target for one draw, and restores them.
            // Get that wrong -- release the window's brushes, leave st->rt on the freed WIC
            // target -- and the editor is dead the next time it paints. Case 1 established a
            // real hwnd device before the render, so this is the actual restored set.
            check(livePaint(hwnd), "a real WM_PAINT after the offscreen render still succeeds");
            // And the offscreen path itself still works after a live paint, byte for byte.
            const bool again = sentinelide::d2dEditorRenderToPng(hwnd, pngPath.c_str());
            check(again, "a second render through the same state still succeeds");
            Image im2;
            const char* why = "";
            const bool ok2 = again && loadPng(wic, pngPath.c_str(), im2, why);
            check(ok2, "and it decodes");
            if (ok2 && im2.w == im.w && im2.h == im.h) {
                long long same = 0, diff = 0;
                for (size_t i = 0; i + 3 < im.px.size(); i += 4) {
                    if (im.px[i] == im2.px[i] && im.px[i + 1] == im2.px[i + 1] &&
                        im.px[i + 2] == im2.px[i + 2])
                        ++same;
                    else
                        ++diff;
                }
                printf("     %lld pixels identical, %lld differ between the two renders\n", same,
                       diff);
                check(diff == 0, "byte-identical to the first render - device state intact");
            } else {
                check(false, "second image has the same dimensions");
            }
        }

        printf("\n7. ERROR-LINE TINTS are painted decoration, and they clear\n");
        {
            // The tints came back at slice 4 as something the control PAINTS, because the
            // RichEdit way of doing it (select each line, set a character background) moves
            // the selection once per diagnostic and would collapse undo granularity around
            // every build. Nothing in this case touches the text, the selection or the undo
            // stack -- which is the property being demonstrated as much as the colour.
            const Rgb tint = fromColorRef(sentinelide::blendColor(th.windowBg, th.diagError, 24));
            printf("     tint = blendColor(windowBg, diagError, 24) = (%d,%d,%d)\n", tint.r,
                   tint.g, tint.b);
            const LONG y0 = lineTopY(hwnd, 0);
            const LONG lineH = lineTopY(hwnd, 1) - y0;
            const int kErrLine = 3;
            // Sample well to the RIGHT of any text, in the middle of the line's band: the
            // band spans the full client width, and nothing there depends on a glyph.
            const UINT sx = im.w - 4;
            const UINT syErr = static_cast<UINT>(lineTopY(hwnd, kErrLine) + lineH / 2);
            const UINT syOk = static_cast<UINT>(y0 + lineH / 2);

            std::vector<int> lines;
            lines.push_back(kErrLine);
            lines.push_back(kErrLine);  // a duplicate: two diagnostics on one line is normal
            sentinelide::d2dEditorSetErrorLines(hwnd, lines);
            Image tinted;
            const char* why = "";
            const bool okTint = sentinelide::d2dEditorRenderToPng(hwnd, tintPath.c_str()) &&
                                loadPng(wic, tintPath.c_str(), tinted, why) &&
                                tinted.w == im.w && tinted.h == im.h;
            check(okTint, "rendered again with line 3 marked as an error");
            if (okTint) {
                printf("     line %d band pixel (%u,%u) = (%d,%d,%d)\n", kErrLine, sx, syErr,
                       tinted.at(sx, syErr).r, tinted.at(sx, syErr).g, tinted.at(sx, syErr).b);
                check(chanDist(tinted.at(sx, syErr), tint) <= 2,
                      "the marked line's band is the RichEdit tint colour");
                check(chanDist(tinted.at(sx, syOk), bg) <= 2,
                      "an unmarked line is still plain windowBg");
                // The band goes BEHIND the text, it does not replace it.
                long long c3[kPalCount];
                countClasses(tinted, pal, bg, static_cast<UINT>(lineTopY(hwnd, kErrLine)),
                             static_cast<UINT>(lineTopY(hwnd, kErrLine) + lineH), c3);
                printClasses("line 3, tinted", c3);
                check(c3[kPalKeyword] >= 40, "and the syntax colouring still shows through it");
            }

            // Clearing is the same call with an empty list -- clearErrorMarks' whole
            // implementation on this control. The proof that it left NOTHING behind is that
            // the image goes back to being byte-identical to the very first render.
            sentinelide::d2dEditorSetErrorLines(hwnd, std::vector<int>());
            Image cleared;
            const bool okClear = sentinelide::d2dEditorRenderToPng(hwnd, pngPath.c_str()) &&
                                 loadPng(wic, pngPath.c_str(), cleared, why) &&
                                 cleared.w == im.w && cleared.h == im.h;
            check(okClear, "rendered again with the error lines cleared");
            if (okClear) {
                check(chanDist(cleared.at(sx, syErr), bg) <= 2, "the band is gone");
                long long diff = 0;
                for (size_t i = 0; i + 3 < im.px.size(); i += 4) {
                    if (im.px[i] != cleared.px[i] || im.px[i + 1] != cleared.px[i + 1] ||
                        im.px[i + 2] != cleared.px[i + 2])
                        ++diff;
                }
                printf("     %lld pixels differ from the un-tinted render\n", diff);
                check(diff == 0, "byte-identical to the original render - nothing left behind");
            }
        }
    }

    if (hwnd) DestroyWindow(hwnd);
    sentinelide::SafeRelease(wic);
    CoUninitialize();

    printf("\nPNG left at: %ls\n", pngPath.c_str());
    printf("error-tint PNG at: %ls\n", tintPath.c_str());
    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
