#pragma once
#include "apx.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <string>
#include <string_view>

namespace aps {
// Attempt Playback names are deliberately narrow: a level save ID, then the
// fixed suffix. Keeping this check exact prevents path tricks from the cloud.
inline bool safeName(std::string const& n) {
    constexpr std::string_view suffix="_attempts.apx";
    if(n.size()<=suffix.size() || !std::string_view(n).ends_with(suffix)) return false;
    for(size_t i=0;i<n.size()-suffix.size();++i) {
        char ch=n[i];
        if(!(ch>='A'&&ch<='Z') && !(ch>='a'&&ch<='z') && !(ch>='0'&&ch<='9') && ch!='_' && ch!='-') return false;
    }
    return true;
}

// Blob SHAs are content hashes. Remembering the SHA plus a hash of the local
// file lets an unchanged level skip its download entirely on later syncs.
struct CacheEntry { std::string sha; uint64_t hash=0; };
using Cache=std::map<std::string,CacheEntry>;
inline uint64_t fingerprint(Bytes const& b) {
    uint64_t h=14695981039346656037ull;
    for(auto c:b) { h^=c; h*=1099511628211ull; }
    return h;
}
inline Cache loadCache(std::filesystem::path const& p) {
    Cache cache; std::ifstream in(p,std::ios::binary); if(!in) return cache;
    for(std::string name,sha,hash;in>>name>>sha>>hash;) {
        if(!safeName(name) || sha.size()!=40) continue;
        try { cache[name]={sha,std::stoull(hash,nullptr,16)}; } catch(...) { continue; }
        if(cache.size()>=1000) break;
    }
    return cache;
}
inline void saveCache(std::filesystem::path const& p,Cache const& cache) {
    try {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p,std::ios::binary|std::ios::trunc); if(!out) return;
        for(auto const& [name,entry]:cache) out<<name<<' '<<entry.sha<<' '<<std::hex<<entry.hash<<'\n';
    } catch(...) {}
}
}
