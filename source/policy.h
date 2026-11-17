// Rules and limits that depend on neither Windows nor Direct3D, so they can
// be tested on any platform (see config-test.cpp).
#pragma once
#include <cstdint>

namespace rrpolicy {

// One display mode offered to the game. `refresh` is in Hz.
struct Mode { unsigned width, height, refresh; };

// Upper bounds: modes in the configuration, and adapters that get added modes.
constexpr unsigned maxModes = 256, maxAdapters = 32;

// Offered when there is no usable RaceRoomTriple.ini: three QHD, three Full HD
// and three 4K screens side by side, each at 60 and 144 Hz.
constexpr Mode defaults[] = {
    {7680, 1440, 60}, {7680, 1440, 144},
    {5760, 1080, 60}, {5760, 1080, 144},
    {11520, 2160, 60}, {11520, 2160, 144}
};

// Whether a device or swap chain may be created. `parameters` is false if the
// game passed no presentation parameters. Only windowed mode is allowed.
inline bool allowPresentation(bool parameters, bool windowed) { return parameters && windowed; }

// True if `index` addresses one of the `count` modes that are appended after
// the adapter's `nativeCount` own modes.
inline bool extraIndex(unsigned nativeCount, unsigned index, unsigned count) {
    return index >= nativeCount && index - nativeCount < count;
}

// Plausibility check for a combined resolution.
//   strict:      exactly 48:9, which is three 16:9 screens of the same size
//   not strict:  anything from square to 12:1, for example bezel-corrected widths
inline bool validDimensions(unsigned w, unsigned h, bool strict) {
    if (w < 320 || h < 200 || w > 16384 || h > 16384) return false;
    if (std::uint64_t(w) * h > 134217728u) return false;  // 2^27 pixels in total
    return strict ? std::uint64_t(w) * 9 == std::uint64_t(h) * 48
                  : w >= h && std::uint64_t(w) <= std::uint64_t(h) * 12;
}

// True if the mode fits the adapter's size limits. A limit of 0 means
// "unknown" and never excludes a mode.
inline bool withinCaps(const Mode& m, unsigned w, unsigned h) {
    return (!w || m.width <= w) && (!h || m.height <= h);
}

}  // namespace rrpolicy
