#pragma once
#include "policy.h"
#include <cstddef>
#include <cstring>
namespace rrconfig {
constexpr std::size_t maxBytes=65536, maxLine=512;
enum class Problem { Syntax, SizeOrAspect, Frequency, Capacity, Unknown, LineTooLong };
struct Issue { unsigned line; Problem reason; };
struct Config {
    rrpolicy::Mode modes[rrpolicy::maxModes]{};
    unsigned count=0, duplicates=0, issueCount=0, storedIssues=0;
    Issue issues[32]{};
    bool strict=true;
};
inline void problem(Config& c,unsigned line,Problem reason) {
    ++c.issueCount;
    if(c.storedIssues<32) c.issues[c.storedIssues++]={line,reason};
}
inline const char* reasonName(Problem p) {
    switch(p) {
        case Problem::Syntax:return "syntax";
        case Problem::SizeOrAspect:return "dimensions/aspect";
        case Problem::Frequency:return "refresh-rate";
        case Problem::Capacity:return "mode-limit";
        case Problem::Unknown:return "unknown-key/section";
        case Problem::LineTooLong:return "line-too-long";
    }
    return "unknown";
}
inline bool space(char c) { return c==' ' || c=='\t' || c=='\r'; }
inline char lower(char c) { return c>='A'&&c<='Z' ? char(c-'A'+'a') : c; }
inline bool equal(const char* a,const char* b) {
    while(*a && *b) if(lower(*a++)!=lower(*b++)) return false;
    return *a==*b;
}
inline char* trim(char* s) {
    while(space(*s)) ++s;
    std::size_t n=std::strlen(s);
    while(n && space(s[n-1])) s[--n]=0;
    return s;
}
inline bool number(const char*& p,unsigned& out) {
    while(space(*p)) ++p;
    if(*p<'0' || *p>'9') return false;
    std::uint64_t n=0;
    while(*p>='0' && *p<='9') {
        n=n*10+unsigned(*p++-'0');
        if(n>0xffffffffu) return false;
    }
    while(space(*p)) ++p;
    out=unsigned(n); return true;
}
struct Lines {
    const char* data; std::size_t size, pos=0; unsigned line=0;
    bool next(char (&out)[maxLine],bool& tooLong) {
        if(pos>=size) return false;
        const std::size_t first=pos;
        while(pos<size && data[pos]!='\n') ++pos;
        const std::size_t n=pos-first;
        if(pos<size) ++pos;
        ++line; tooLong=n>=maxLine;
        if(tooLong) { out[0]=0; return true; }
        std::memcpy(out,data+first,n); out[n]=0;
        for(std::size_t i=0;i<n;++i) if(out[i]==';' || out[i]=='#') {out[i]=0;break;}
        return true;
    }
};
inline void add(Config& c,unsigned w,unsigned h,unsigned hz,unsigned line) {
    for(unsigned i=0;i<c.count;++i)
        if(c.modes[i].width==w && c.modes[i].height==h && c.modes[i].refresh==hz) {
            ++c.duplicates; return;
        }
    if(c.count==rrpolicy::maxModes) {problem(c,line,Problem::Capacity);return;}
    c.modes[c.count++]={w,h,hz};
}
inline void defaultModes(Config& c) {
    c.count=0;
    for(const auto& m:rrpolicy::defaults) c.modes[c.count++]=m;
}
// No allocations, file access or Windows dependencies. Returns false only for
// invalid encoding/size. Valid rows survive unrelated malformed rows.
inline bool parse(const char* data,std::size_t size,Config& c) {
    c=Config{};
    if(!data || size>maxBytes) return false;
    if(size>=3 && static_cast<unsigned char>(data[0])==0xef &&
       static_cast<unsigned char>(data[1])==0xbb && static_cast<unsigned char>(data[2])==0xbf) {
        data+=3;size-=3;
    }
    for(std::size_t i=0;i<size;++i) if(data[i]==0) return false;
    // Two passes make Settings independent of section order.
    for(unsigned pass=0;pass<2;++pass) {
        Lines lines{data,size}; char buffer[maxLine]; bool longLine=false;
        enum Section { None,Settings,Modes,Other } section=None;
        while(lines.next(buffer,longLine)) {
            if(longLine) { if(pass==1) problem(c,lines.line,Problem::LineTooLong); continue; }
            char* s=trim(buffer); if(!*s) continue;
            if(*s=='[') {
                const std::size_t n=std::strlen(s);
                if(n<3 || s[n-1]!=']') {
                    section=Other;
                    if(pass==1) problem(c,lines.line,Problem::Syntax);
                    continue;
                }
                s[n-1]=0;s=trim(s+1);
                section=equal(s,"Settings")?Settings:equal(s,"Modes")?Modes:Other;
                if(section==Other && pass==1) problem(c,lines.line,Problem::Unknown);
                continue;
            }
            if((pass==0 && section!=Settings) || (pass==1 && section==Settings)) continue;
            char* eq=std::strchr(s,'=');
            if(!eq) {problem(c,lines.line,Problem::Syntax);continue;}
            *eq=0; char* key=trim(s); char* value=trim(eq+1);
            if(section==Settings) {
                if(!equal(key,"StrictTripleAspect")) {problem(c,lines.line,Problem::Unknown);continue;}
                if(equal(value,"true")) c.strict=true;
                else if(equal(value,"false")) c.strict=false;
                else problem(c,lines.line,Problem::Syntax);
                continue;
            }
            if(section!=Modes) {problem(c,lines.line,Problem::Unknown);continue;}
            const char* k=key;unsigned w=0,h=0;
            if(!number(k,w) || (*k!='x' && *k!='X')) {problem(c,lines.line,Problem::Syntax);continue;}
            ++k;
            if(!number(k,h) || *k) {problem(c,lines.line,Problem::Syntax);continue;}
            if(!rrpolicy::validDimensions(w,h,c.strict)) {problem(c,lines.line,Problem::SizeOrAspect);continue;}
            const char* v=value; unsigned rates[64],count=0;bool ok=true;
            do {
                unsigned hz=0;
                if(count==64 || !number(v,hz) || hz<24 || hz>1000) {ok=false;break;}
                rates[count++]=hz;
                if(!*v) break;
                if(*v++!=',') {ok=false;break;}
                if(!*v) {ok=false;break;}
            } while(true);
            if(!ok || !count) {problem(c,lines.line,Problem::Frequency);continue;}
            for(unsigned i=0;i<count;++i) add(c,w,h,rates[i],lines.line);
        }
    }
    return true;
}
}
