#pragma once
#include <cstdint>
namespace rrpolicy {
struct Mode { unsigned width, height, refresh; };
constexpr unsigned maxModes=256, maxAdapters=32;
constexpr Mode defaults[]={
    {7680,1440,60},{7680,1440,144},
    {5760,1080,60},{5760,1080,144},
    {11520,2160,60},{11520,2160,144}
};
inline bool allowPresentation(bool parameters,bool windowed) { return parameters && windowed; }
inline bool extraIndex(unsigned nativeCount,unsigned index,unsigned count) {
    return index>=nativeCount && index-nativeCount<count;
}
inline bool validDimensions(unsigned w,unsigned h,bool strict) {
    if(w<320 || h<200 || w>16384 || h>16384) return false;
    if(std::uint64_t(w)*h>134217728u) return false;
    return strict ? std::uint64_t(w)*9==std::uint64_t(h)*48
                  : w>=h && std::uint64_t(w)<=std::uint64_t(h)*12;
}
inline bool withinCaps(const Mode& m,unsigned w,unsigned h) {
    return (!w || m.width<=w) && (!h || m.height<=h);
}
}
