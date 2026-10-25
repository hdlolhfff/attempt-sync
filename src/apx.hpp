#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace aps {
using Bytes = std::vector<uint8_t>;
constexpr size_t maxBytes = 64 * 1024 * 1024;
inline void require(bool b, const char* s) { if (!b) throw std::runtime_error(s); }
inline uint32_t u32(Bytes const& b, size_t p) {
    require(p <= b.size() && b.size()-p >= 4, "Truncated APX data");
    return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24;
}
inline void put(Bytes& b, size_t p, uint32_t v) {
    require(p <= b.size() && b.size()-p >= 4, "Invalid APX write");
    for (int i=0;i<4;++i) b[p+i]=uint8_t(v>>(i*8));
}
inline void append32(Bytes& b,uint32_t v) { auto p=b.size(); b.resize(p+4); put(b,p,v); }
inline Bytes slice(Bytes const& b,size_t p,size_t n) {
    require(p<=b.size() && n<=b.size()-p,"Truncated APX chunk");
    return Bytes(b.begin()+p,b.begin()+p+n);
}
struct File { std::vector<Bytes> attempts, sessions; Bytes pathHeader; };
// ATT3 v2 uses fixed-width packed fields; all supported CPUs are little-endian.
inline void track(Bytes const& b, size_t& p, uint32_t count) {
    auto n=u32(b,p); p+=4;
    require(n<=b.size()-p && count<=10'000'000 && count<=n,"Invalid APX track");
    auto end=p+n;
    for(uint32_t i=0;i<count;++i) {
        require(p<end,"Truncated APX frame"); auto tag=b[p++];
        require((tag & ~uint8_t(31))==0 && (i!=0 || (tag&1)),"Invalid APX frame tag");
        size_t need=(tag&1)?22:8;
        if(!(tag&1)) { if(tag&2) ++need; if(tag&4) ++need; if(tag&8) need+=2; if(tag&16) need+=2; }
        require(need<=end-p,"Truncated APX frame data"); p+=need;
    }
    require(p==end,"Trailing APX track data");
}
inline File parse(Bytes const& b) {
    File f; if(b.empty()) return f;
    require(b.size()<=maxBytes,"APX file exceeds 64 MiB sync limit");
    require(b.size()>=8 && std::string(b.begin(),b.begin()+4)=="APX2","Invalid APX magic");
    require(u32(b,4)==8,"Only APX v8 is supported; open the level in Attempt Playback 1.5.7 first");
    size_t p=8;
    while(p<b.size()) {
        auto type=u32(b,p), n=u32(b,p+4); p+=8;
        auto data=slice(b,p,n); p+=n;
        if(type==0x33545441) {
            require(data.size()>=56 && data[14]==2 && data[15]==0,"Unsupported ATT3 version");
            require(u32(data,0)>0 && u32(data,0)<=0x7fffffff,"Invalid attempt serial");
            size_t q=48; track(data,q,u32(data,16)); track(data,q,u32(data,20));
            require(q==data.size(),"Trailing ATT3 data"); f.attempts.push_back(std::move(data));
        } else if(type==0x48544150) {
            require(data.size()>=20 && u32(data,0)==5,"Unsupported practice PATH");
            // The writer appends updated PATH snapshots. Like Attempt Playback,
            // use the last complete snapshot rather than combining stale routes.
            f.pathHeader=slice(data,0,20); f.sessions.clear(); auto count=u32(data,4);
            require(count<=100'000,"Too many practice sessions"); size_t q=20;
            for(uint32_t i=0;i<count;++i) {
                auto start=q; auto a=u32(data,q+4), s=u32(data,q+8);
                require(a<=1'000'000 && s<=1'000'000,"Too many practice items");
                size_t size=28+size_t(a)*4+size_t(s)*48;
                f.sessions.push_back(slice(data,start,size)); q+=size;
            }
            require(q==data.size(),"Trailing PATH data");
        } else throw std::runtime_error("Unknown or legacy APX chunk; file left untouched");
    }
    std::map<uint32_t,bool> serials;
    for(auto const& a:f.attempts) require(serials.emplace(u32(a,0),true).second,"Duplicate attempt serial");
    return f;
}
inline void chunk(Bytes& out,uint32_t type,Bytes const& b) {
    require(out.size()+8+b.size()<=maxBytes,"Merged APX exceeds 64 MiB sync limit");
    append32(out,type); append32(out,uint32_t(b.size())); out.insert(out.end(),b.begin(),b.end());
}
// Remote is the baseline so repeated syncs do not continually reorder serials.
// Compare complete payloads with only the per-device serial/padding normalized.
inline Bytes merge(Bytes const& remote,Bytes const& local) {
    auto r=parse(remote), l=parse(local);
    std::vector<Bytes> attempts,sessions;
    std::map<Bytes,uint32_t> seenAttempts,seenSessions;
    std::map<uint32_t,uint32_t> rm,lm,rs,ls;
    auto addAttempts=[&](File const& f,auto& ids) {
        for(auto a:f.attempts) {
            auto old=u32(a,0); put(a,0,0); a[13]=0;
            auto [it,added]=seenAttempts.emplace(a,uint32_t(attempts.size()+1));
            ids[old]=it->second;
            if(added) { put(a,0,it->second); attempts.push_back(std::move(a)); }
        }
    };
    addAttempts(r,rm); addAttempts(l,lm);
    auto remap=[&](Bytes& b,size_t p,auto const& ids) {
        auto old=u32(b,p); auto it=ids.find(old);
        require(it!=ids.end(),"Practice session references a missing attempt"); put(b,p,it->second);
    };
    auto addSessions=[&](File const& f,auto const& ids,auto& sessionIds) {
        for(auto s:f.sessions) {
            auto old=u32(s,0); auto a=u32(s,4), n=u32(s,8);
            put(s,0,0); s[26]=s[27]=0;
            for(uint32_t i=0;i<a;++i) remap(s,28+size_t(i)*4,ids);
            for(uint32_t i=0;i<n;++i) remap(s,28+size_t(a)*4+size_t(i)*48+8,ids);
            auto [it,added]=seenSessions.emplace(s,uint32_t(sessions.size()+1));
            sessionIds[old]=it->second;
            if(added) { put(s,0,it->second); sessions.push_back(std::move(s)); }
        }
    };
    addSessions(r,rm,rs); addSessions(l,lm,ls);
    Bytes out={'A','P','X','2',8,0,0,0};
    for(auto const& a:attempts) chunk(out,0x33545441,a);
    Bytes path=r.pathHeader.empty()?l.pathHeader:r.pathHeader;
    auto const& sessionIds=r.pathHeader.empty()?ls:rs;
    if(path.empty()) { path.resize(20); put(path,0,5); put(path,8,0xffffffff); put(path,12,0xffffffff); }
    put(path,4,uint32_t(sessions.size())); path[17]=path[18]=path[19]=0;
    for(size_t p:{size_t(8),size_t(12)}) {
        auto it=sessionIds.find(u32(path,p)); put(path,p,it==sessionIds.end()?0xffffffff:it->second);
    }
    for(auto const& s:sessions) path.insert(path.end(),s.begin(),s.end());
    chunk(out,0x48544150,path); return out;
}
inline std::string base64(Bytes const& b) {
    constexpr char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s; s.reserve((b.size()+2)/3*4);
    for(size_t i=0;i<b.size();i+=3) {
        uint32_t v=uint32_t(b[i])<<16;
        if(i+1<b.size()) v|=uint32_t(b[i+1])<<8;
        if(i+2<b.size()) v|=b[i+2];
        s+=t[(v>>18)&63]; s+=t[(v>>12)&63];
        s+=i+1<b.size()?t[(v>>6)&63]:'='; s+=i+2<b.size()?t[v&63]:'=';
    } return s;
}
}
