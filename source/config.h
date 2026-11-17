// Parser for RaceRoomTriple.ini.
//
// Deliberately free of Windows headers, file access and dynamic memory, so the
// same code runs inside the DLL and in the portable tests (config-test.cpp).
//
// File format:
//
//   [Settings]
//   StrictTripleAspect = true        ; true: sizes must be exactly 48:9
//
//   [Modes]
//   7680x1440 = 60,75,100,120,144    ; WIDTHxHEIGHT = refresh rates in Hz
//
// Comments start with ';' or '#'. Section names, keys and true/false ignore
// upper and lower case.
#pragma once
#include "policy.h"
#include <cstddef>
#include <cstring>

namespace rrconfig {

// Hard limits: file size in bytes, and line length in bytes including the
// terminator (so a line may hold 511 characters).
constexpr std::size_t maxBytes = 65536, maxLine = 512;

// Why a line of the INI was not used. reasonName() turns it into log text.
enum class Problem { Syntax, SizeOrAspect, Frequency, Capacity, Unknown, LineTooLong };

// One rejected line.
struct Issue { unsigned line; Problem reason; };

// The result of parsing.
struct Config {
    rrpolicy::Mode modes[rrpolicy::maxModes]{};  // accepted modes, in file order
    unsigned count = 0;         // used entries in modes
    unsigned duplicates = 0;    // repeated width/height/refresh combinations that were dropped
    unsigned issueCount = 0;    // all problems found
    unsigned storedIssues = 0;  // problems kept in issues (only the first 32)
    Issue issues[32]{};
    bool strict = true;         // the StrictTripleAspect setting
};

// Records a problem. All are counted, the first 32 are kept for the log.
inline void problem(Config& c, unsigned line, Problem reason) {
    ++c.issueCount;
    if (c.storedIssues < 32) c.issues[c.storedIssues++] = {line, reason};
}

inline const char* reasonName(Problem p) {
    switch (p) {
        case Problem::Syntax: return "syntax";
        case Problem::SizeOrAspect: return "dimensions/aspect";
        case Problem::Frequency: return "refresh-rate";
        case Problem::Capacity: return "mode-limit";
        case Problem::Unknown: return "unknown-key/section";
        case Problem::LineTooLong: return "line-too-long";
    }
    return "unknown";
}

// ---- Small text helpers, ASCII only ----

// Blank, tab or the carriage return left over from Windows line ends.
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

// Compares two strings without regard to upper and lower case.
inline bool equal(const char* a, const char* b) {
    while (*a && *b) if (lower(*a++) != lower(*b++)) return false;
    return *a == *b;
}

// Removes white space at both ends. Cuts the end off in place and returns a
// pointer to the first character that is not white space.
inline char* trim(char* s) {
    while (space(*s)) ++s;
    std::size_t n = std::strlen(s);
    while (n && space(s[n - 1])) s[--n] = 0;
    return s;
}

// Reads a non-negative whole number with optional white space around it and
// moves `p` past it. Fails if there is no digit or the value exceeds 32 bits.
inline bool number(const char*& p, unsigned& out) {
    while (space(*p)) ++p;
    if (*p < '0' || *p > '9') return false;
    std::uint64_t n = 0;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + unsigned(*p++ - '0');
        if (n > 0xffffffffu) return false;
    }
    while (space(*p)) ++p;
    out = unsigned(n);
    return true;
}

// Walks through the file contents line by line.
struct Lines {
    const char* data;
    std::size_t size, pos = 0;
    unsigned line = 0;  // number of the line returned last, counted from 1

    // Copies the next line into `out` and cuts off any comment. Returns false
    // at the end of the data. A line that does not fit is reported through
    // `tooLong` and comes back empty.
    bool next(char (&out)[maxLine], bool& tooLong) {
        if (pos >= size) return false;
        const std::size_t first = pos;
        while (pos < size && data[pos] != '\n') ++pos;
        const std::size_t n = pos - first;
        if (pos < size) ++pos;  // step over the line feed
        ++line;
        tooLong = n >= maxLine;
        if (tooLong) {
            out[0] = 0;
            return true;
        }
        std::memcpy(out, data + first, n);
        out[n] = 0;
        for (std::size_t i = 0; i < n; ++i) if (out[i] == ';' || out[i] == '#') {
            out[i] = 0;
            break;
        }
        return true;
    }
};

// Adds a mode unless it is already in the list or the list is full.
inline void add(Config& c, unsigned w, unsigned h, unsigned hz, unsigned line) {
    for (unsigned i = 0; i < c.count; ++i)
        if (c.modes[i].width == w && c.modes[i].height == h && c.modes[i].refresh == hz) {
            ++c.duplicates;
            return;
        }
    if (c.count == rrpolicy::maxModes) {
        problem(c, line, Problem::Capacity);
        return;
    }
    c.modes[c.count++] = {w, h, hz};
}

// Replaces the mode list with the built-in defaults. Recorded problems stay,
// so the log can still show why the INI was not used.
inline void defaultModes(Config& c) {
    c.count = 0;
    for (const auto& m : rrpolicy::defaults) c.modes[c.count++] = m;
}

// Parses the contents of the INI file into `c`.
//
// No allocations, file access or Windows dependencies. Returns false only if
// the data as a whole is unusable: null pointer, too large, or a zero byte in
// it (which is what a file saved as UTF-16 looks like). A faulty line is
// recorded as a problem and skipped; the valid lines around it still count.
inline bool parse(const char* data, std::size_t size, Config& c) {
    c = Config{};
    if (!data || size > maxBytes) return false;

    // Skip a UTF-8 byte order mark, as written by some editors.
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb && static_cast<unsigned char>(data[2]) == 0xbf) {
        data += 3;
        size -= 3;
    }
    for (std::size_t i = 0; i < size; ++i) if (data[i] == 0) return false;

    // Two passes make Settings independent of section order: pass 0 reads
    // only [Settings], pass 1 everything else. That way StrictTripleAspect
    // applies to all modes, even if [Modes] comes first in the file.
    for (unsigned pass = 0; pass < 2; ++pass) {
        Lines lines{data, size};
        char buffer[maxLine];
        bool longLine = false;
        enum Section { None, Settings, Modes, Other } section = None;

        while (lines.next(buffer, longLine)) {
            if (longLine) {
                if (pass == 1) problem(c, lines.line, Problem::LineTooLong);
                continue;
            }
            char* s = trim(buffer);
            if (!*s) continue;  // empty line or comment only

            // Section header: [Name]
            if (*s == '[') {
                const std::size_t n = std::strlen(s);
                if (n < 3 || s[n - 1] != ']') {
                    section = Other;
                    if (pass == 1) problem(c, lines.line, Problem::Syntax);
                    continue;
                }
                s[n - 1] = 0;
                s = trim(s + 1);
                section = equal(s, "Settings") ? Settings : equal(s, "Modes") ? Modes : Other;
                if (section == Other && pass == 1) problem(c, lines.line, Problem::Unknown);
                continue;
            }

            // Leave out the lines that belong to the other pass.
            if ((pass == 0 && section != Settings) || (pass == 1 && section == Settings)) continue;

            // Everything else must be "key = value".
            char* eq = std::strchr(s, '=');
            if (!eq) {
                problem(c, lines.line, Problem::Syntax);
                continue;
            }
            *eq = 0;
            char* key = trim(s);
            char* value = trim(eq + 1);

            // [Settings]: the only known key is StrictTripleAspect = true|false.
            if (section == Settings) {
                if (!equal(key, "StrictTripleAspect")) {
                    problem(c, lines.line, Problem::Unknown);
                    continue;
                }
                if (equal(value, "true")) c.strict = true;
                else if (equal(value, "false")) c.strict = false;
                else problem(c, lines.line, Problem::Syntax);
                continue;
            }

            // A key outside any section, or inside an unknown one.
            if (section != Modes) {
                problem(c, lines.line, Problem::Unknown);
                continue;
            }

            // [Modes]: the key is WIDTHxHEIGHT ...
            const char* k = key;
            unsigned w = 0, h = 0;
            if (!number(k, w) || (*k != 'x' && *k != 'X')) {
                problem(c, lines.line, Problem::Syntax);
                continue;
            }
            ++k;
            if (!number(k, h) || *k) {
                problem(c, lines.line, Problem::Syntax);
                continue;
            }
            if (!rrpolicy::validDimensions(w, h, c.strict)) {
                problem(c, lines.line, Problem::SizeOrAspect);
                continue;
            }

            // ... and the value is a comma-separated list of 1 to 64 refresh
            // rates from 24 to 1000 Hz. One bad entry rejects the whole line.
            const char* v = value;
            unsigned rates[64], count = 0;
            bool ok = true;
            do {
                unsigned hz = 0;
                if (count == 64 || !number(v, hz) || hz < 24 || hz > 1000) {
                    ok = false;
                    break;
                }
                rates[count++] = hz;
                if (!*v) break;  // end of the list
                if (*v++ != ',') {
                    ok = false;
                    break;
                }
                if (!*v) {  // comma with nothing after it
                    ok = false;
                    break;
                }
            } while (true);
            if (!ok || !count) {
                problem(c, lines.line, Problem::Frequency);
                continue;
            }
            for (unsigned i = 0; i < count; ++i) add(c, w, h, rates[i], lines.line);
        }
    }
    return true;
}

}  // namespace rrconfig
