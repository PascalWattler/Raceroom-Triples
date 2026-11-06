#include "config.h"
#include <cassert>
#include <string>
#include <limits>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace rrconfig;
static Config parseText(const std::string& s) {
    Config c;assert(parse(s.data(),s.size(),c));return c;
}
int main(int argc,char** argv){
    if(argc==2) {
        std::ifstream input(argv[1],std::ios::binary);assert(input);
        std::string actual((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        const Config example=parseText(actual);
        assert(example.count==15 && example.issueCount==0 && example.strict);
    }
    auto c=parseText("[Settings]\nStrictTripleAspect=true\n[Modes]\n7680x1440=60,75,100,120,144\n5760x1080=60\n11520x2160=144\n");
    assert(c.count==7 && c.issueCount==0 && c.modes[1].refresh==75 && c.modes[2].refresh==100 && c.modes[3].refresh==120);
    c=parseText("\xef\xbb\xbf[MODES]\r\n 7680 X 1440 = 60,144 ; note\r\n7680x1440=144,60\n");
    assert(c.count==2 && c.duplicates==2 && !c.issueCount);
    c=parseText("[Modes]\n7860x1440=120\n[Settings]\nStrictTripleAspect=false\n");
    assert(c.count==1 && !c.strict);
    c=parseText("[Modes]\n7860x1440=120\n7680x1440=60\n");
    assert(c.count==1 && c.issueCount==1 && c.modes[0].width==7680);
    c=parseText("[Modes]\n7680x1440=0\n7680x1440=23\n7680x1440=1001\n7680x1440=-60\n7680x1440=60,\n7680x1440=60,garbage\n7680x1440=60 75\n7680x1440=24,1000\n");
    assert(c.count==2 && c.issueCount==7);
    c=parseText("[Modes]\n42949672960x1440=60\n7680x42949672960=60\n7680x1440=42949672960\n0x0=60\n7680x1440junk=60\n7680x1440=60\n");
    assert(c.count==1 && c.issueCount==5);
    c=parseText("[Modes]\n7680x1440=60\n[Settings]\nStrictTripleAspect=garbage\nUnknown=1\n[Wrong]\nThing=60\n");
    assert(c.count==1 && c.issueCount==4 && c.strict);
    c=parseText("[Modes]\n"+std::string(600,'x')+"\n7680x1440=144\n");
    assert(c.count==1 && c.issueCount==1);
    c=parseText("[Settings]\nStrictTripleAspect=false\n[Modes]\n16384x16384=60\n16385x1440=60\n300x200=60\n320x200=60\n11520x2160=120\n");
    assert(c.count==2 && c.issueCount==3);
    std::string many="[Modes]\n";
    for(unsigned hz=24;hz<400;++hz) many+="7680x1440="+std::to_string(hz)+"\n";
    c=parseText(many);assert(c.count==rrpolicy::maxModes && c.issueCount==376-rrpolicy::maxModes);
    Config invalid;
    const char embedded[]={ '[','M','o','d','e','s',']',0,'\n' };
    assert(!parse(embedded,sizeof(embedded),invalid));
    assert(!parse(nullptr,0,invalid));
    const std::string oversized(maxBytes+1,' ');
    assert(!parse(oversized.data(),oversized.size(),invalid));
    c=parseText("");assert(c.count==0);defaultModes(c);assert(c.count==6 && c.modes[0].width==7680);
    assert(rrpolicy::withinCaps({11520,2160,144},16384,16384));
    assert(!rrpolicy::withinCaps({11520,2160,144},8192,8192));
    assert(rrpolicy::withinCaps({7680,1440,144},0,0));
    assert(rrpolicy::extraIndex(217,217,15));
    assert(rrpolicy::extraIndex(217,231,15));
    assert(!rrpolicy::extraIndex(217,232,15));
    assert(!rrpolicy::extraIndex(217,216,15));
    assert(!rrpolicy::extraIndex(217,std::numeric_limits<unsigned>::max(),15));
    assert(!rrpolicy::extraIndex(0,0,0));
    assert(rrpolicy::allowPresentation(true,true));
    assert(!rrpolicy::allowPresentation(true,false));
    assert(!rrpolicy::allowPresentation(false,true));
    // Deterministic malformed-input exercise; sanitizer builds verify bounds.
    unsigned rng=0x12345678;
    for(unsigned n=0;n<3000;++n) {
        rng=rng*1664525u+1013904223u;
        std::string fuzz(rng%2048,' ');
        for(char& ch:fuzz) {rng=rng*1664525u+1013904223u;ch=char((rng>>16)&255);}
        Config f;parse(fuzz.data(),fuzz.size(),f);
        assert(f.count<=rrpolicy::maxModes && f.storedIssues<=32);
    }
    std::puts("Config tests passed: parsing, bounds, ratios, frequencies, duplicate handling, fallback, caps, indices and malformed input.");
}
