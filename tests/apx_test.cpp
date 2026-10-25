#include "../src/sync_policy.hpp"
#include "../src/sync_cache.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <cstring>
using namespace aps;
Bytes attempt(uint32_t serial,uint8_t marker) {
    Bytes b(48); put(b,0,serial); b[14]=2; put(b,16,1);
    append32(b,23); b.push_back(1); b.resize(b.size()+22); b[b.size()-22]=marker;
    append32(b,0); return b;
}
// Packed v5 layout from Attempt Playback's published APX format. Serialize
// the actual fields, independently of the sync parser's offset constants.
#pragma pack(push, 1)
struct PracticeSegmentV5 {
    float startX,endX;
    int32_t ownerSerial,p1Frames,p2Frames;
    float maxXReached;
    double baseTimeOffset,localStartTime,localEndTime;
};
#pragma pack(pop)
static_assert(sizeof(PracticeSegmentV5)==48);
Bytes session(uint32_t id,uint32_t serial) {
    Bytes s(28); put(s,0,id); put(s,4,1); put(s,8,1);
    append32(s,serial);
    PracticeSegmentV5 segment{0.f,100.f,int32_t(serial),1,0,100.f,0.,0.,1.};
    auto p=s.size(); s.resize(p+sizeof(segment));
    std::memcpy(s.data()+p,&segment,sizeof(segment));
    return s;
}
Bytes file(std::vector<Bytes> const& attempts,std::vector<Bytes> const& sessions={}) {
    Bytes b={'A','P','X','2',8,0,0,0};
    for(auto const& a:attempts) chunk(b,0x33545441,a);
    Bytes path(20); put(path,0,5); put(path,4,uint32_t(sessions.size()));
    put(path,8,sessions.empty()?0xffffffff:u32(sessions[0],0));
    put(path,12,sessions.empty()?0xffffffff:u32(sessions[0],0));
    for(auto const& s:sessions) path.insert(path.end(),s.begin(),s.end());
    chunk(b,0x48544150,path); return b;
}
template<class F> void rejects(F fn) { bool failed=false; try { fn(); } catch(std::exception const&) { failed=true; } assert(failed); }
int main() {
    auto a=file({attempt(1,10)},{session(1,1)});
    auto b=file({attempt(1,20)},{session(1,1)});
    // Multiple PATH chunks are normal: the last saved snapshot takes priority.
    auto appendHistory=file({attempt(1,10),attempt(2,20)},{session(1,1)});
    auto newestPath=file({}, {session(7,2)});
    appendHistory.insert(appendHistory.end(),newestPath.begin()+8,newestPath.end());
    auto history=parse(appendHistory);
    assert(history.sessions.size()==1 && u32(history.sessions[0],0)==7);
    assert(u32(history.pathHeader,8)==7 && u32(history.sessions[0],28)==2);
    assert(parse(merge({},appendHistory)).sessions.size()==1);
    auto comparison=compareFiles(a,b);
    assert(comparison.localBytes==comparison.cloudBytes);
    assert(comparison.localOnly==1 && comparison.cloudOnly==1);
    auto larger=file({attempt(1,10),attempt(2,20)});
    comparison=compareFiles(a,larger);
    assert(comparison.localBytes>comparison.cloudBytes && comparison.localAttempts==2 && comparison.cloudAttempts==1);
    assert(comparison.localOnly==1 && comparison.cloudOnly==0);
    comparison=compareFiles(larger,a);
    assert(comparison.localOnly==0 && comparison.cloudOnly==1);
    // A 55-file review includes every file, with separate upload/import counts.
    BatchReview batch;
    for(int i=0;i<55;++i) batch.add(compareFiles({},b),true,true);
    assert(batch.files==55 && batch.uploads==55 && batch.downloads==55);
    assert(batch.localBytes==55*b.size() && batch.cloudBytes==0);
    assert(batch.localAttempts==55*parse(b).attempts.size());
    assert(batch.localLarger==55 && batch.localOnly==55 && batch.cloudOnly==0);
    BatchReview mixed;
    mixed.add(compareFiles(a,b),true,true);
    mixed.add(compareFiles(a,{}),false,true);
    assert(mixed.files==2 && mixed.uploads==1 && mixed.downloads==2);
    assert(mixed.localOnly==1 && mixed.cloudOnly==2);
    auto view=planSync(a,b,false);
    assert(!view.upload && parse(view.imported).attempts.size()==2);
    assert(!planSync({},b,false).upload);
    assert(planSync(a,b,true).upload);
    assert(!planSync(a,a,true).upload);
    assert(std::string(playbackRepository)=="hdlolhfff/gd-attempts");
    assert(std::string(playbackBranch)=="main");
    auto ab=merge(a,b); auto f=parse(ab);
    assert(f.attempts.size()==2 && f.sessions.size()==2);
    assert(u32(f.attempts[1],0)==2 && u32(f.sessions[1],28)==2 && u32(f.sessions[1],40)==2);
    assert(u32(f.pathHeader,8)==1);
    assert(merge(ab,a)==ab && merge(ab,b)==ab && merge(ab,ab)==ab);
    auto duplicate=file({attempt(99,10)},{session(90,99)});
    assert(merge(a,duplicate)==a);
    comparison=compareFiles(a,duplicate);
    assert(comparison.localOnly==0 && comparison.cloudOnly==0);
    assert(merge({},a)==a && merge(a,{})==a);
    auto orphan=file({attempt(1,10)},{session(1,2)});
    rejects([&]{ merge(a,orphan); });
    auto dupSerial=file({attempt(1,10),attempt(1,20)});
    rejects([&]{ parse(dupSerial); });
    auto unknown=a; put(unknown,8,123); rejects([&]{parse(unknown);});
    auto future=a; put(future,4,9); rejects([&]{parse(future);});
    auto malformed=attempt(1,10); put(malformed,48,0xffffffff);
    rejects([&]{parse(file({malformed}));});
    for(size_t i=1;i<a.size();++i) {
        if(i==8 || i==8+8+attempt(1,10).size()) continue; // valid chunk-boundary files
        rejects([&]{parse(Bytes(a.begin(),a.begin()+i));});
    }
    assert(base64({})=="" && base64({'f'})=="Zg==" && base64({'f','o'})=="Zm8=" && base64({'f','o','o'})=="Zm9v");
    // Convergence across devices and repeated overlapping histories.
    auto cloud=a;
    for(uint32_t i=2;i<100;++i) cloud=merge(cloud,file({attempt(1,uint8_t(i))}));
    assert(parse(cloud).attempts.size()==98); // marker 10 was already in a
    assert(merge(cloud,a)==cloud);
    std::mt19937 rng(42);
    for(int i=0;i<5000;++i) {
        auto damaged=a; damaged[rng()%damaged.size()]=uint8_t(rng());
        try { auto m=merge(a,damaged); parse(m); } catch(std::exception const&) {}
    }
    // Unchanged-file cache: name validation, hashing, and round-trip parsing.
    assert(safeName("100080980_attempts.apx") && safeName("064e00d68c351e17_attempts.apx"));
    assert(!safeName("_attempts.apx") && !safeName("100080980_attempts.apx.bak"));
    assert(!safeName("a/b_attempts.apx") && !safeName("a b_attempts.apx"));
    assert(fingerprint(Bytes{1,2,3})==fingerprint(Bytes{1,2,3}));
    assert(fingerprint(Bytes{1,2,3})!=fingerprint(Bytes{1,2,4}));
    auto cachePath=std::filesystem::temp_directory_path()/"attempt-sync-cache-test.txt";
    std::filesystem::remove(cachePath);
    assert(loadCache(cachePath).empty());
    Cache cached;
    cached["100080980_attempts.apx"]={"0123456789012345678901234567890123456789",0x123456789abcdef0ull};
    saveCache(cachePath,cached);
    auto reloaded=loadCache(cachePath);
    assert(reloaded.size()==1 && reloaded["100080980_attempts.apx"].sha==cached["100080980_attempts.apx"].sha);
    assert(reloaded["100080980_attempts.apx"].hash==cached["100080980_attempts.apx"].hash);
    { std::ofstream out(cachePath,std::ios::trunc); out<<"../evil_attempts.apx 0123456789012345678901234567890123456789 1\n"; }
    assert(loadCache(cachePath).empty());
    std::filesystem::remove(cachePath);
    std::cout<<"APX merge, serial remapping, practice sessions, convergence, truncation, malformed input, base64, and sync cache checks passed.\n";
}
