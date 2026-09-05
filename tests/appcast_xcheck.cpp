// SPDX-License-Identifier: GPL-3.0-or-later
//
// appcast_xcheck — the cross-check for the LAST parser to leave C++: the update-feed
// reader (src/sentinel/parsers.sentinel::parse_appcast, compiled into
// build/generated/parsers.lib). It is also the only one of the six whose input arrives
// off the NETWORK, over deliberately unauthenticated HTTPS, so it is the one where
// "what does this do with bytes an attacker chose" is not a hypothetical.
//
// THREE implementations are run against every case and all three are asserted:
//
//   oracle::    a VERBATIM copy of the C++ that shipped — Updater.cpp's parseVersion,
//               versionIsNewer and appcastVersion at b5a8d0b. Parity is measured
//               against what shipped, not against a tidied-up memory of it.
//   fallback::  a VERBATIM copy of Updater.cpp's `#else` branch, the C++ used when a
//               build has no parsers.lib. It must agree with Sentinel EVERYWHERE — the
//               two are one behaviour with two spellings, and this is what pins them.
//   Sentinel    parse_appcast itself.
//
// WHERE THE ORACLE IS WRONG THIS TEST ASSERTS THE NEW BEHAVIOUR AND SAYS SO. Every case
// carries an explicit Parity/Diverges expectation plus the reason, and a Diverges case
// FAILS if the two ever agree again — so nobody can quietly restore the old behaviour
// and still be green. The two defects the port exists to close are exactly the
// divergences:
//
//   1. UNBOUNDED SIGNED OVERFLOW. `out[i] = out[i] * 10 + (*v - '0')` accumulated into
//      an `int` over however many digits the feed supplied. Cases 7, 8 and 18 print the
//      actual numbers the oracle computes.
//   2. NO VALIDATION. appcastVersion returned whatever sat between the quotes, verbatim,
//      and the host showed it, logged it, and on "Skip this version" wrote it into
//      settings.ini. Cases 3, 9, 10, 14, 15 and 16 are feeds where the oracle OFFERS an
//      update whose "version" is not a version.
//
// Build+run (needs build\generated\parsers.lib, i.e. a build.bat run first):
//     cmake --build build --target appcast_xcheck
//     build\appcast_xcheck.exe appcast.xml
// or via ctest (which passes the repo's real appcast.xml as argv[1]).

#include <cstdint>
#include <cstdio>
#include <string>

extern "C" {
    void parse_appcast(const uint8_t*, int64_t, const uint8_t*, int64_t, uint8_t**, int64_t*);
    void sentinel_free_bytes(uint8_t*);
}

// ---------------------------------------------------------------------------
// The oracle: src/host/win32/Updater.cpp at b5a8d0b, copied verbatim. Only the
// enclosing namespace is added. DO NOT "fix" anything in here — its bugs are the
// measurement.
// ---------------------------------------------------------------------------
namespace oracle {

// Parse "a.b.c.d" into four numbers. Missing components read as 0, so "0.1.6" == "0.1.6.0".
void parseVersion(const char* v, int out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    int i = 0;
    while (v && *v && i < 4) {
        while (*v >= '0' && *v <= '9') { out[i] = out[i] * 10 + (*v - '0'); ++v; }
        if (*v == '.') { ++v; ++i; } else break;
    }
}
bool versionIsNewer(const char* candidate, const char* mine) {
    int a[4], b[4];
    parseVersion(candidate, a);
    parseVersion(mine, b);
    for (int i = 0; i < 4; ++i) { if (a[i] != b[i]) return a[i] > b[i]; }
    return false;
}

// Pull sparkle:version="..." out of the feed. We publish this file, so a targeted match
// beats dragging in an XML parser.
std::string appcastVersion(const std::string& body) {
    const std::string key = "sparkle:version=\"";
    const size_t a = body.find(key);
    if (a == std::string::npos) return {};
    const size_t b = body.find('"', a + key.size());
    if (b == std::string::npos) return {};
    return body.substr(a + key.size(), b - (a + key.size()));
}

// The two shipped call sites both did exactly this: take the string, refuse an empty
// one, and otherwise compare. Reproduced here so "what the oracle would have done"
// means what the IDE would have done, not what the functions return in isolation.
void run(const std::string& body, const std::string& mine, std::string& ver, bool& newer) {
    ver = appcastVersion(body);
    newer = !ver.empty() && versionIsNewer(ver.c_str(), mine.c_str());
}

}  // namespace oracle

// ---------------------------------------------------------------------------
// The fallback: src/host/win32/Updater.cpp's `#else` branch (plus AppcastVerdict,
// which is declared above the #ifdef there), copied verbatim. This is the C++ a
// snc-less build compiles. It must match Sentinel on every case below.
// ---------------------------------------------------------------------------
namespace fallback {

struct AppcastVerdict {
    bool found = false;      // sparkle:version="…" located (opening AND closing quote)
    bool valid = false;      // ... and what sat between them is a well-formed version
    bool newer = false;      // ... and it is strictly newer than the running build
    std::string version;     // the version — ONLY when valid; empty otherwise
};

// The version grammar, and the only one accepted: 1..4 components, 1..9 digits each,
// single dots, nothing else. Nine digits caps a component at 999,999,999, so
// acVersionComp's accumulate cannot overflow a long long — and a longer component
// rejects the WHOLE version rather than wrapping or being truncated, because a number
// the feed never stated is exactly what decides an update the wrong way.
bool acVersionValid(const std::string& v) {
    if (v.empty() || v.size() > 39) return false;
    int dots = 0, digits = 0;
    for (char c : v) {
        if (c == '.') {
            if (digits == 0 || ++dots > 3) return false;   // empty component / 5th component
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 9) return false;                // the overflow bound
        } else {
            return false;                                  // not a digit or a dot
        }
    }
    return digits > 0;                                     // rejects a trailing '.'
}
// Component k (0-based); missing trailing components read as 0, so "0.1.6" == "0.1.6.0".
// PRECONDITION: acVersionValid(v). The `digits < 9` guard is therefore unreachable and
// is written anyway, so this cannot overflow for any input at all.
long long acVersionComp(const std::string& v, int k) {
    int cur = 0, digits = 0;
    long long acc = 0;
    for (char c : v) {
        if (c == '.') { cur++; digits = 0; }
        else if (cur == k && c >= '0' && c <= '9' && digits < 9) { acc = acc * 10 + (c - '0'); digits++; }
    }
    return acc;
}
AppcastVerdict readAppcast(const std::string& body, const std::string& mine) {
    AppcastVerdict v;
    // FIRST match, not highest — see parse_appcast for why. Searching for the name
    // without the quote and then requiring the quote reproduces find()ing the whole
    // `sparkle:version="` literal: an unquoted occurrence is not a match and the scan
    // continues past it, but once the quote is seen the search stops either way.
    const std::string key = "sparkle:version=";
    for (size_t at = body.find(key); at != std::string::npos; at = body.find(key, at + 1)) {
        if (at + key.size() >= body.size() || body[at + key.size()] != '"') continue;
        const size_t s = at + key.size() + 1;
        const size_t q = body.find('"', s);
        if (q != std::string::npos) { v.found = true; v.version.assign(body, s, q - s); }
        break;
    }
    if (!v.found) return v;
    v.valid = acVersionValid(v.version);
    if (!v.valid) { v.version.clear(); return v; }   // an invalid version is never handed back
    if (!acVersionValid(mine)) return v;             // our own version unreadable -> nothing is newer
    for (int i = 0; i < 4; i++) {
        const long long a = acVersionComp(v.version, i), b = acVersionComp(mine, i);
        if (a != b) { v.newer = a > b; break; }
    }
    return v;
}

}  // namespace fallback

// ---- Sentinel side: the same call Updater.cpp's #ifdef branch makes ----------
static fallback::AppcastVerdict sentinelReadAppcast(const std::string& body, const std::string& mine) {
    fallback::AppcastVerdict v;
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_appcast((const uint8_t*)body.data(), (int64_t)body.size(),
                  (const uint8_t*)mine.data(), (int64_t)mine.size(), &out, &olen);
    if (out && olen >= 11) {
        v.found = out[0] == 1;
        v.valid = out[1] == 1;
        v.newer = out[2] == 1;
        uint64_t n = 0;
        for (int i = 0; i < 8; i++) n |= (uint64_t)out[3 + i] << (8 * i);
        if (v.valid && n > 0 && (uint64_t)olen >= 11 + n)
            v.version.assign((const char*)out + 11, (size_t)n);
    }
    if (out) sentinel_free_bytes(out);
    return v;
}

// ---- harness ---------------------------------------------------------------
enum class Rel { Parity, Diverges };

static int pass = 0, fail = 0;

// A minimal but realistic appcast around one sparkle:version value.
static std::string feed(const std::string& ver) {
    return
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<rss version=\"2.0\" xmlns:sparkle=\"http://www.andymatuschak.org/xml-namespaces/sparkle\">\n"
        "  <channel>\n"
        "    <title>Sentinel-IDE Updates</title>\n"
        "    <item>\n"
        "      <title>Version " + ver + "</title>\n"
        "      <enclosure url=\"https://example.invalid/Sentinel-IDE-setup.exe\"\n"
        "                 sparkle:version=\"" + ver + "\"\n"
        "                 sparkle:edSignature=\"AAAA\"\n"
        "                 length=\"1\"\n"
        "                 type=\"application/octet-stream\"/>\n"
        "    </item>\n"
        "  </channel>\n"
        "</rss>\n";
}

static void check(const char* name, const std::string& body, const std::string& mine,
                  bool eFound, bool eValid, bool eNewer, const char* eVersion,
                  Rel rel, const char* why) {
    const fallback::AppcastVerdict s = sentinelReadAppcast(body, mine);
    const fallback::AppcastVerdict f = fallback::readAppcast(body, mine);
    std::string ov;
    bool on = false;
    oracle::run(body, mine, ov, on);

    // (a) Sentinel does what this test says it must.
    const bool asserted = s.found == eFound && s.valid == eValid && s.newer == eNewer &&
                          s.version == std::string(eVersion);
    // (b) the C++ fallback is the same behaviour, not a second opinion.
    const bool agree = f.found == s.found && f.valid == s.valid && f.newer == s.newer &&
                       f.version == s.version;
    // (c) the relationship to the shipped C++ is the one we declared.
    const bool same = (ov == s.version) && (on == s.newer);
    const bool relOk = (rel == Rel::Parity) ? same : !same;

    const bool ok = asserted && agree && relOk;
    printf("[%s] %-46s %s\n", ok ? "PASS" : "FAIL", name,
           rel == Rel::Parity ? "(parity)" : "(DIVERGES)");
    if (!ok || rel == Rel::Diverges) {
        int oc[4] = {0, 0, 0, 0};
        oracle::parseVersion(ov.c_str(), oc);
        printf("        shipped C++ : version=\"%s\" -> [%d,%d,%d,%d] offer=%s\n",
               ov.c_str(), oc[0], oc[1], oc[2], oc[3], on ? "YES" : "no");
        printf("        Sentinel    : found=%d valid=%d newer=%d version=\"%s\"\n",
               s.found, s.valid, s.newer, s.version.c_str());
        if (rel == Rel::Diverges) printf("        why         : %s\n", why);
    }
    if (!ok) {
        if (!asserted)
            printf("        !! expected found=%d valid=%d newer=%d version=\"%s\"\n",
                   eFound, eValid, eNewer, eVersion);
        if (!agree)
            printf("        !! C++ fallback disagrees with Sentinel: found=%d valid=%d newer=%d version=\"%s\"\n",
                   f.found, f.valid, f.newer, f.version.c_str());
        if (!relOk)
            printf("        !! expected %s against the shipped C++, got the opposite\n",
                   rel == Rel::Parity ? "parity" : "a divergence");
    }
    if (ok) pass++; else fail++;
}

// The last dotted component of a version, or -1 if there isn't a numeric one. Used only
// to build "one build older" / "one build newer" out of whatever the feed publishes.
static long long lastComponent(const std::string& v) {
    const size_t dot = v.find_last_of('.');
    const std::string tail = (dot == std::string::npos) ? v : v.substr(dot + 1);
    if (tail.empty()) return -1;
    long long n = 0;
    for (char c : tail) { if (c < '0' || c > '9') return -1; n = n * 10 + (c - '0'); }
    return n;
}
static std::string bumpLast(const std::string& v, int delta) {
    const size_t dot = v.find_last_of('.');
    const std::string head = (dot == std::string::npos) ? std::string() : v.substr(0, dot + 1);
    return head + std::to_string(lastComponent(v) + delta);
}

static bool readFileBytes(const char* path, std::string& out) {
    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") != 0 || !fp) return false;
    char buf[4096];
    size_t got = 0;
    while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, got);
    fclose(fp);
    return true;
}

int main(int argc, char** argv) {
    const std::string mine = "0.1.12.196";   // a real shipped SENTINEL_FILEVERSION_STR

    // ---- 1-2: nothing to read. Both refuse; nothing is offered. -------------
    check("1  no sparkle:version in the feed",
          "<rss><channel><item><title>Version 9</title></item></channel></rss>",
          mine, false, false, false, "", Rel::Parity, "");

    check("2  opening quote, no closing quote at all",
          "<enclosure sparkle:version=\"0.1.13.0", mine,
          false, false, false, "", Rel::Parity, "");

    // ---- 3: the same defect with a later quote in the document -------------
    // find('"') does not know what an attribute is, so it walks into the NEXT one.
    check("3  unterminated attribute, later quote in feed",
          "<enclosure sparkle:version=\"0.1.13.0 length=\"1\"/>", mine,
          true, false, false, "", Rel::Diverges,
          "the shipped reader returns '0.1.13.0 length=' and OFFERS it: parseVersion "
          "stops at the space having read 0.1.13.0, and that XML fragment is what the "
          "dialog shows and 'Skip this version' writes into settings.ini");

    // ---- 4: sparkle:version="" ---------------------------------------------
    check("4  empty value between the quotes", feed(""), mine,
          true, false, false, "", Rel::Parity, "");

    // ---- 5-6: THE trap. A string compare says 0.1.10 < 0.1.9. --------------
    check("5  0.1.10 is newer than 0.1.9", feed("0.1.10"), "0.1.9",
          true, true, true, "0.1.10", Rel::Parity, "");
    check("6  0.1.9 is NOT newer than 0.1.10", feed("0.1.9"), "0.1.10",
          true, true, false, "0.1.9", Rel::Parity, "");

    // ---- 7-8: defect 1, unbounded signed overflow ---------------------------
    check("7  14-digit component, single", feed("99999999999999"), mine,
          true, false, false, "", Rel::Diverges,
          "14 digits overflow the shipped `int`: it computes 276447231 (from digits that "
          "state 99999999999999) and OFFERS the feed as newer");
    check("8  10-digit component, wraps negative", feed("0.1.2147483648.0"), mine,
          true, false, false, "", Rel::Diverges,
          "2147483648 wraps to -2147483648, so the shipped reader ranks a hugely higher "
          "version BELOW 0.1.12.196 and suppresses it — the same arithmetic, failing the "
          "other way");

    // ---- 9-10: defect 2, no validation. Both of these get OFFERED. ---------
    check("9  version is prose", feed("99 red balloons"), mine,
          true, false, false, "", Rel::Diverges,
          "parseVersion reads 99, stops at the space, and 99 > 0 — so the shipped reader "
          "offers an update called '99 red balloons' and can persist that string");
    check("10 pre-release tag", feed("0.1.13-rc1"), mine,
          true, false, false, "", Rel::Diverges,
          "reads 0.1.13, stops at '-', offers; the string shown and persisted is "
          "'0.1.13-rc1', which our feed never emits");

    // ---- 11: a 200 that is not an appcast ----------------------------------
    check("11 GitHub 404 HTML page as the body",
          "<!DOCTYPE html>\n<html lang=\"en\">\n<head><title>Page not found &middot; GitHub</title>\n"
          "<meta name=\"viewport\" content=\"width=device-width\">\n</head>\n"
          "<body class=\"px-2\"><h1>404</h1><p>File not found</p>\n"
          "<p>The site configured at this address does not contain the requested file.</p>\n"
          "</body></html>\n",
          mine, false, false, false, "", Rel::Parity, "");

    // ---- 12-13: which occurrence wins --------------------------------------
    // FIRST, deliberately: our published feed carries exactly one <item>, first-match is
    // what shipped, and it bounds what a 256 KB body can steer us to.
    {
        std::string two = feed("0.1.13.200");
        two += feed("9.9.9.9");
        check("12 two entries — the FIRST one wins", two, mine,
              true, true, true, "0.1.13.200", Rel::Parity, "");
    }
    check("13 unquoted occurrence is skipped, next one wins",
          "<a sparkle:version=0.1.99.0 /><b sparkle:version=\"0.1.13.200\" />", mine,
          true, true, true, "0.1.13.200", Rel::Parity, "");

    // ---- 14-16: malformed shapes the oracle silently repairs ---------------
    check("14 trailing dot", feed("0.1.12."), "0.1.11.0",
          true, false, false, "", Rel::Diverges,
          "the shipped reader reads 0.1.12.0 and offers, showing '0.1.12.' as the version");
    check("15 empty component", feed("0.1..12"), "0.1.0.0",
          true, false, false, "", Rel::Diverges,
          "the shipped reader reads 0.1.0.12 and offers, showing '0.1..12'");
    check("16 five components", feed("0.1.12.196.7"), "0.1.12.195",
          true, false, false, "", Rel::Diverges,
          "the shipped reader reads four and IGNORES the fifth, so '1.2.3.4.5' and "
          "'1.2.3.4' compare equal; we refuse rather than compare part of a version. "
          "make-appcast.ps1 enforces ^\\d+\\.\\d+\\.\\d+\\.\\d+$, so this cannot suppress "
          "a real release");

    // ---- 17-18: the digit bound, from both sides ---------------------------
    check("17 nine digits is accepted (the bound)", feed("999999999.0.0.0"), mine,
          true, true, true, "999999999.0.0.0", Rel::Parity, "");
    check("18 ten digits is refused (the cost of the bound)", feed("1000000000.0.0.0"), mine,
          true, false, false, "", Rel::Diverges,
          "1000000000 still fits an int, so the oracle is right here and we are stricter. "
          "This is the price of a bound that cannot be argued with; our build numbers are "
          "git commit counts, so a 10-digit component is not a version we can emit");

    // ---- 19-21: missing trailing components read as 0 ----------------------
    check("19 0.1.6 == 0.1.6.0 (no update)", feed("0.1.6"), "0.1.6.0",
          true, true, false, "0.1.6", Rel::Parity, "");
    check("20 0.1.6.0 == 0.1.6 (no update, reversed)", feed("0.1.6.0"), "0.1.6",
          true, true, false, "0.1.6.0", Rel::Parity, "");
    check("21 0.1.6.1 is newer than 0.1.6", feed("0.1.6.1"), "0.1.6",
          true, true, true, "0.1.6.1", Rel::Parity, "");

    // ---- 22: our OWN version unreadable -> nothing is newer ----------------
    // Unreachable in the product (mine is SENTINEL_FILEVERSION_STR, a compile-time
    // constant), which is exactly why it is worth pinning.
    check("22 unparseable `mine` fails closed", feed("0.1.13.0"), "0.1.12.196.junk",
          true, true, false, "0.1.13.0", Rel::Diverges,
          "the shipped reader parses four components out of our own bad version and "
          "offers; nothing is newer than a version we cannot read");

    // ---- 23-25: the real published feed ------------------------------------
    // The published version is READ OUT of the feed, not pinned here as a literal. It
    // was pinned once, and the release that bumped appcast.xml to 0.1.13.202 turned this
    // test red without changing anything it is about — a maintenance trap that fires
    // every release. What these three cases actually assert is the RELATION: the
    // published version is well-formed, the same version is not an update, one build
    // older is, one build newer is not. The extraction is oracle::appcastVersion, so the
    // string never comes from the code under test.
    {
        const char* path = argc > 1 ? argv[1] : "appcast.xml";
        std::string body;
        if (!readFileBytes(path, body)) {
            printf("[FAIL] could not read the real appcast at \"%s\" — pass its path as argv[1]\n", path);
            fail++;
        } else {
            const std::string pub = oracle::appcastVersion(body);
            const long long last = lastComponent(pub);
            if (!fallback::acVersionValid(pub) || last < 1) {
                printf("[FAIL] the published feed's sparkle:version is \"%s\" — not a version this "
                       "build could ever offer, or a build number below 1\n", pub.c_str());
                fail++;
            } else {
                printf("       (published feed says %s)\n", pub.c_str());
                check("23 real appcast.xml, same version (no update)", body, pub,
                      true, true, false, pub.c_str(), Rel::Parity, "");
                check("24 real appcast.xml, older build (update)", body, bumpLast(pub, -1),
                      true, true, true, pub.c_str(), Rel::Parity, "");
                check("25 real appcast.xml, newer build (no update)", body, bumpLast(pub, +1),
                      true, true, false, pub.c_str(), Rel::Parity, "");
            }
        }
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
