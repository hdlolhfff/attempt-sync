#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <fstream>
#include <set>
#include <regex>
#include "sync_policy.hpp"

using namespace geode::prelude;
namespace fs = std::filesystem;
using geode::utils::web::WebRequest;
using geode::utils::web::WebResponse;

namespace {
struct Import { fs::path path; aps::Bytes before, after; };
struct Outcome { std::vector<Import> imports; std::string message; bool failed=false; int synced=0; };
struct Config { std::string token; fs::path dir; bool owner=false; };
bool running=false;
std::optional<Outcome> pending;
std::string status="Playbacks come from hdlolhfff/gd-attempts. Tap Sync to download.";

aps::Bytes read(fs::path const& p) {
    if(!fs::exists(p)) return {};
    aps::require(fs::is_regular_file(p) && !fs::is_symlink(p),"Attempt file must be a regular file");
    auto n=fs::file_size(p); aps::require(n>=8,"Attempt file is empty or truncated"); aps::require(n<=aps::maxBytes,"Attempt file exceeds 20 MiB");
    std::ifstream in(p,std::ios::binary); aps::require(bool(in),"Cannot open attempt file");
    aps::Bytes b(n); in.read(reinterpret_cast<char*>(b.data()),std::streamsize(n));
    aps::require(bool(in),"Cannot read attempt file"); return b;
}
void write(fs::path const& p,aps::Bytes const& b) {
    std::ofstream out(p,std::ios::binary|std::ios::trunc);
    aps::require(bool(out),"Cannot write sync file");
    out.write(reinterpret_cast<char const*>(b.data()),std::streamsize(b.size())); out.close();
    aps::require(bool(out),"Cannot finish writing sync file");
}
std::string str(matjson::Value const& j,char const* k) {
    auto r=j[k].asString(); aps::require(r.isOk(),"Unexpected GitHub response"); return r.unwrap();
}
WebRequest request(Config const& c,bool raw=false) {
    WebRequest req;
    if(!c.token.empty()) req.header("Authorization","Bearer "+c.token);
    req.header("Accept",raw?"application/vnd.github.raw+json":"application/vnd.github+json")
       .header("X-GitHub-Api-Version","2022-11-28")
       .header("User-Agent","AttemptSync/1.1")
       .timeout(std::chrono::seconds(45));
    return req;
}
void ok(WebResponse const& r) {
    if(!r.ok()) throw std::runtime_error("GitHub request failed (HTTP "+std::to_string(r.code())+"). Check connection, repository, branch and token permissions; retry later.");
}
matjson::Value json(WebResponse const& r) {
    ok(r); auto j=r.json(); aps::require(j.isOk(),"Invalid GitHub JSON"); return j.unwrap();
}
bool safeName(std::string const& n) { return std::regex_match(n,std::regex("[A-Za-z0-9_-]+_attempts\\.apx")); }

arc::Future<Outcome> sync(Config c) {
    Outcome out;
    size_t importBytes=0;
    try {
        auto base=std::string("https://api.github.com/repos/")+aps::playbackRepository;
        // Resolve branch first: a missing repo/branch must not be mistaken for an empty folder.
        auto branchResponse=co_await request(c).get(base+"/branches/"+aps::playbackBranch);
        if(branchResponse.code()==404 && c.token.empty())
            throw std::runtime_error("gd-attempts is private. Add your own authorized GitHub token in settings to access it.");
        auto branch=json(branchResponse);
        auto commit=str(branch["commit"],"sha");
        auto listResponse=co_await request(c).param("ref",commit).get(base+"/contents/attempts");
        std::set<std::string> names;
        if(listResponse.code()!=404) {
            auto list=json(listResponse); aps::require(list.isArray(),"GitHub attempts path must be a folder");
            aps::require(list.size()<1000,"GitHub folder listing limit reached (1000 files)");
            for(auto const& item:list) {
                auto n=str(item,"name");
                if(str(item,"type")=="file" && safeName(n)) names.insert(n);
            }
        }
        fs::create_directories(c.dir);
        if(c.owner) for(auto const& entry:fs::directory_iterator(c.dir)) {
            auto name=entry.path().filename().string();
            if(entry.is_regular_file() && safeName(name)) names.insert(name);
        }
        aps::require(names.size()<1000,"Too many level files to sync");
        for(auto const& name:names) {
            auto path=c.dir/name;
            auto local=read(path);
            bool done=false;
            for(int retry=0;retry<3 && !done;++retry) {
                auto endpoint=base+"/contents/attempts/"+name;
                auto metadataResponse=co_await request(c).param("ref",c.owner?std::string(aps::playbackBranch):commit).get(endpoint);
                aps::Bytes remote; std::string sha;
                if(metadataResponse.code()!=404) {
                    auto meta=json(metadataResponse);
                    aps::require(str(meta,"type")=="file","Remote attempt path is not a file");
                    auto size=meta["size"].asInt();
                    aps::require(size.isOk() && size.unwrap()>=0 && size.unwrap()<=int64_t(aps::maxBytes),"Remote attempt exceeds 20 MiB");
                    sha=str(meta,"sha");
                    // Read immutable blob, so content and compare-and-swap SHA agree.
                    auto blob=co_await request(c,true).get(base+"/git/blobs/"+sha);
                    ok(blob); remote=blob.data();
                    aps::require(remote.size()==size_t(size.unwrap()),"Remote blob size mismatch");
                    aps::require(remote.size()>=8,"Remote attempt file is empty or truncated");
                }
                aps::require(c.owner || !remote.empty(),"Playback missing from the selected cloud snapshot");
                auto plan=aps::planSync(remote,local,c.owner);
                auto merged=std::move(plan.imported);
                if(plan.upload) {
                    auto body=matjson::Value::object();
                    body["message"]="Sync Attempt Playback: "+name;
                    body["branch"]=aps::playbackBranch;
                    body["content"]=aps::base64(merged);
                    if(!sha.empty()) body["sha"]=sha;
                    auto uploaded=co_await request(c).bodyJSON(body).put(endpoint);
                    if(uploaded.code()==409 || uploaded.code()==422) continue;
                    ok(uploaded);
                }
                if(merged!=local) {
                    importBytes+=local.size()+merged.size();
                    aps::require(importBytes<=128*1024*1024,"Download batch exceeds 128 MiB. Completed files will import; sync again to continue.");
                    out.imports.push_back({path,local,std::move(merged)});
                }
                ++out.synced; done=true;
            }
            aps::require(done,"Another device kept changing this file. Retry sync.");
        }
        out.message=(c.owner?"Synced ":"Downloaded playbacks for ")+std::to_string(out.synced)+" levels.";
        if(!c.owner && out.synced==0) out.message="No playbacks published yet. The owner needs to enable Upload My Attempts and sync first.";
    } catch(std::exception const& e) {
        out.failed=true; out.message=e.what();
    }
    co_return out;
}
// Installation happens on the main thread only, outside gameplay. If the player
// leaves the menu during a request, imports wait for their return.
void installPending() {
    if(!pending || GameManager::get()->m_playLayer || GameManager::get()->m_levelEditorLayer) return;
    auto out=std::move(*pending); pending.reset();
    int skipped=0;
    try {
        auto root=Mod::get()->getSaveDir()/"backups";
        for(auto const& item:out.imports) {
            if(read(item.path)!=item.before) { ++skipped; continue; }
            auto backup=root/std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            fs::create_directories(backup);
            auto name=item.path.filename();
            auto temp=item.path; temp+=".sync-tmp";
            write(temp,item.after);
            // Keep the original in a separate directory. Rename rather than delete
            // on Windows, where rename cannot replace an existing destination.
            bool hadOriginal=fs::exists(item.path);
            if(hadOriginal) fs::rename(item.path,backup/name);
            try { fs::rename(temp,item.path); }
            catch(...) { if(hadOriginal) fs::rename(backup/name,item.path); throw; }
        }
        status=out.message;
        if(skipped) status+=" Local files changed during sync; tap Sync again to merge them.";
        if(out.failed) status+=" Completed files were kept; retry to finish.";
    } catch(std::exception const& e) { out.failed=true; status=std::string("Import failed: ")+e.what()+". Original files are in backups."; }
    running=false;
    if(out.failed) FLAlertLayer::create("Attempt Sync",status,"OK")->show();
    Notification::create(out.failed?"Attempt Sync needs attention":"Attempt Sync finished",
                         out.failed?NotificationIcon::Warning:NotificationIcon::Success)->show();
}
void start() {
    installPending();
    if(running) { FLAlertLayer::create("Attempt Sync",status,"OK")->show(); return; }
    try {
        auto mod=Mod::get(); Config c;
        c.owner=mod->getSettingValue<bool>("upload-my-attempts");
        c.token=mod->getSettingValue<std::string>("github-token");
        aps::require(!c.owner || !c.token.empty(),"To upload your attempts, add your GitHub token with Contents read/write access to gd-attempts in settings.");
        auto playback=Loader::get()->getLoadedMod("manshoety.attemptplayback");
        aps::require(playback!=nullptr,"Enable Attempt Playback 1.5.7 first.");
        aps::require(playback->getVersion()==VersionInfo(1,5,7),"This sync format was verified for Attempt Playback 1.5.7. Use that version until this mod is updated.");
        c.dir=playback->getSaveDir()/"attempts";
        running=true; status="Syncing with GitHub. You can keep playing; downloads apply at the main menu.";
        async::spawn(sync(std::move(c)),[](Outcome out) {
            pending=std::move(out); status="Downloads ready; return to the main menu to apply them."; installPending();
        });
    } catch(std::exception const& e) { status=e.what(); FLAlertLayer::create("Attempt Sync",status,"OK")->show(); }
}
}
class $modify(SyncMenu,MenuLayer) {
    bool init() {
        if(!MenuLayer::init()) return false;
        installPending();
        auto menu=CCMenu::create(); menu->setPosition({0,0});
        auto sprite=ButtonSprite::create("Sync",0,0,"bigFont.fnt","GJ_button_01.png",0.f,0.65f);
        auto button=CCMenuItemSpriteExtra::create(sprite,this,menu_selector(SyncMenu::onSync));
        button->setID("attempt-sync-button");
        button->setPosition({CCDirector::sharedDirector()->getWinSize().width-48,100});
        menu->addChild(button); this->addChild(menu,100);
        auto mod=Mod::get();
        if(mod->getSettingValue<bool>("auto-sync") && !running &&
           (!mod->getSettingValue<bool>("upload-my-attempts") ||
            !mod->getSettingValue<std::string>("github-token").empty())) start();
        return true;
    }
    void onSync(CCObject*) { start(); }
};
