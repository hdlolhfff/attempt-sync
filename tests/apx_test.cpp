#include "../src/sync_policy.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace aps;
Bytes attempt(uint32_t serial,uint8_t marker) {
    Bytes b(48); put(b,0,serial); b[14]=2; put(b,16,1);
    append32(b,23); b.push_back(1); b.resize(b.size()+22); b[b.size()-22]=marker;
    append32(b,0); return b;
}
Bytes session(uint32_t id,uint32_t serial) {
    Bytes s(28); put(s,0,id); put(s,4,1); put(s,8,1);
    append32(s,serial); auto p=s.size(); s.resize(p+52); put(s,p+8,serial); return s;
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
    auto comparison=compareFiles(a,b);
    assert(comparison.localBytes==comparison.cloudBytes);
    assert(comparison.localOnly==1 && comparison.cloudOnly==1);
    auto larger=file({attempt(1,10),attempt(2,20)});
    comparison=compareFiles(a,larger);
    assert(comparison.localBytes>comparison.cloudBytes && comparison.localAttempts==2 && comparison.cloudAttempts==1);
    assert(comparison.localOnly==1 && comparison.cloudOnly==0);
    comparison=compareFiles(larger,a);
    assert(comparison.localOnly==0 && comparison.cloudOnly==1);
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
    std::cout<<"APX merge, serial remapping, practice sessions, convergence, truncation, malformed input, and base64 checks passed.\n";
}
