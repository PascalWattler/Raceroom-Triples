// Portable tests for the INI parser (config.h) and the rules in policy.h.
// They need neither Windows nor Direct3D. Called with a file name, the
// program first checks that file as the shipped example INI.
//
// The exit code is 0 if every check holds and 1 otherwise.
#include "config.h"
#include <string>
#include <limits>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace rrconfig;

// Number of checks that did not hold.
static int failures = 0;

// Reports a failed check and carries on, so one run shows every failure.
// Unlike assert() it cannot be switched off by a build option.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::printf("FAILED, line %d: %s\n", __LINE__, #condition); \
            ++failures; \
        } \
    } while (false)

// Parses a text that the parser must accept as a whole.
static Config parseText(const std::string& s) {
    Config c;
    const bool accepted = parse(s.data(), s.size(), c);
    CHECK(accepted);
    return c;
}

int main(int argc, char** argv) {
    // Optional: the example INI from the user package. It must give 15 modes
    // (3 resolutions with 5 refresh rates each) and no problems.
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        CHECK(input.is_open());
        std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const Config example = parseText(actual);
        CHECK(example.count == 15 && example.issueCount == 0 && example.strict);
    }

    // A plain, valid file: 5 + 1 + 1 modes, in file order.
    auto c = parseText("[Settings]\nStrictTripleAspect=true\n[Modes]\n7680x1440=60,75,100,120,144\n5760x1080=60\n11520x2160=144\n");
    CHECK(c.count == 7 && c.issueCount == 0 && c.modes[1].refresh == 75 && c.modes[2].refresh == 100 && c.modes[3].refresh == 120);

    // Byte order mark, upper-case section name, Windows line ends, blanks
    // around the x, a trailing comment, and the same modes given twice.
    c = parseText("\xef\xbb\xbf[MODES]\r\n 7680 X 1440 = 60,144 ; note\r\n7680x1440=144,60\n");
    CHECK(c.count == 2 && c.duplicates == 2 && !c.issueCount);

    // [Settings] after [Modes] still applies: a size that is not 48:9 is
    // accepted once StrictTripleAspect is false ...
    c = parseText("[Modes]\n7860x1440=120\n[Settings]\nStrictTripleAspect=false\n");
    CHECK(c.count == 1 && !c.strict);

    // ... and rejected by default.
    c = parseText("[Modes]\n7860x1440=120\n7680x1440=60\n");
    CHECK(c.count == 1 && c.issueCount == 1 && c.modes[0].width == 7680);

    // Refresh rates: seven faulty lines (too low, too high, negative, trailing
    // comma, text, missing comma), then one valid line with both limits.
    c = parseText("[Modes]\n7680x1440=0\n7680x1440=23\n7680x1440=1001\n7680x1440=-60\n7680x1440=60,\n7680x1440=60,garbage\n7680x1440=60 75\n7680x1440=24,1000\n");
    CHECK(c.count == 2 && c.issueCount == 7);

    // Numbers beyond 32 bits, a zero size and text after the size.
    c = parseText("[Modes]\n42949672960x1440=60\n7680x42949672960=60\n7680x1440=42949672960\n0x0=60\n7680x1440junk=60\n7680x1440=60\n");
    CHECK(c.count == 1 && c.issueCount == 5);

    // A bad setting value, an unknown key, an unknown section and its content.
    c = parseText("[Modes]\n7680x1440=60\n[Settings]\nStrictTripleAspect=garbage\nUnknown=1\n[Wrong]\nThing=60\n");
    CHECK(c.count == 1 && c.issueCount == 4 && c.strict);

    // An overlong line is skipped; the line after it is still read.
    c = parseText("[Modes]\n" + std::string(600, 'x') + "\n7680x1440=144\n");
    CHECK(c.count == 1 && c.issueCount == 1);

    // Size limits without the 48:9 rule: too many pixels, too wide and too
    // narrow are rejected; the smallest allowed size and 3 x 4K pass.
    c = parseText("[Settings]\nStrictTripleAspect=false\n[Modes]\n16384x16384=60\n16385x1440=60\n300x200=60\n320x200=60\n11520x2160=120\n");
    CHECK(c.count == 2 && c.issueCount == 3);

    // More modes than fit: the list stops at the limit, the rest is reported.
    std::string many = "[Modes]\n";
    for (unsigned hz = 24; hz < 400; ++hz) many += "7680x1440=" + std::to_string(hz) + "\n";
    c = parseText(many);
    CHECK(c.count == rrpolicy::maxModes && c.issueCount == 376 - rrpolicy::maxModes);

    // Input that is refused as a whole: a zero byte, no data, too large.
    Config invalid;
    const char embedded[] = { '[', 'M', 'o', 'd', 'e', 's', ']', 0, '\n' };
    CHECK(!parse(embedded, sizeof(embedded), invalid));
    CHECK(!parse(nullptr, 0, invalid));
    const std::string oversized(maxBytes + 1, ' ');
    CHECK(!parse(oversized.data(), oversized.size(), invalid));

    // An empty file gives no modes; the fallback then gives the six defaults.
    c = parseText("");
    CHECK(c.count == 0);
    defaultModes(c);
    CHECK(c.count == 6 && c.modes[0].width == 7680);

    // Adapter size limits; 0 stands for "unknown".
    CHECK(rrpolicy::withinCaps({11520, 2160, 144}, 16384, 16384));
    CHECK(!rrpolicy::withinCaps({11520, 2160, 144}, 8192, 8192));
    CHECK(rrpolicy::withinCaps({7680, 1440, 144}, 0, 0));

    // Which indices belong to the added modes: 217 native modes plus 15 added.
    CHECK(rrpolicy::extraIndex(217, 217, 15));
    CHECK(rrpolicy::extraIndex(217, 231, 15));
    CHECK(!rrpolicy::extraIndex(217, 232, 15));
    CHECK(!rrpolicy::extraIndex(217, 216, 15));
    CHECK(!rrpolicy::extraIndex(217, std::numeric_limits<unsigned>::max(), 15));
    CHECK(!rrpolicy::extraIndex(0, 0, 0));

    // Deterministic malformed-input exercise; sanitizer builds verify bounds.
    unsigned rng = 0x12345678;
    for (unsigned n = 0; n < 3000; ++n) {
        rng = rng * 1664525u + 1013904223u;
        std::string fuzz(rng % 2048, ' ');
        for (char& ch : fuzz) {
            rng = rng * 1664525u + 1013904223u;
            ch = char((rng >> 16) & 255);
        }
        Config f;
        parse(fuzz.data(), fuzz.size(), f);
        CHECK(f.count <= rrpolicy::maxModes && f.storedIssues <= maxIssues);
    }

    if (failures) {
        std::printf("Config tests FAILED: %d check(s) did not hold.\n", failures);
        return 1;
    }
    std::puts("Config tests passed: parsing, bounds, ratios, frequencies, duplicate handling, fallback, caps, indices and malformed input.");
    return 0;
}
