#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/ui/Popup.hpp>
#include <arc/future/Join.hpp>
#include <cstdint>
#include <fstream>
#include <set>
#include <map>
#include <string_view>
#include "sync_cache.hpp"
#include "sync_policy.hpp"

using namespace geode::prelude;
namespace fs = std::filesystem;
using geode::utils::web::WebRequest;
using geode::utils::web::WebResponse;
using geode::utils::web::WebFuture;

namespace {
struct Import { fs::path path; aps::Bytes before, after; };
struct Outcome { std::vector<Import> imports; std::string message; bool failed=false; bool cancelled=false; int synced=0; };
struct Config { std::string token; fs::path dir, cache; bool owner=false; };
bool running=false;
std::optional<Outcome> pending;
std::string status="Playbacks come from hdlolhfff/gd-attempts. Tap Sync to download.";

Ref<Notification> syncNotification;
// One overlay notification is updated in place throughout the job.
void showLiveProgress(std::string const& text) {
    if(!syncNotification) {
        syncNotification=Notification::create(text,NotificationIcon::Loading,0.f);
        syncNotification->show();
    } else syncNotification->setString(text);
}
void finishProgress(std::string const& text,NotificationIcon icon) {
    if(!syncNotification) {
        syncNotification=Notification::create(text,icon,NOTIFICATION_LONG_TIME);
        syncNotification->show();
    } else {
        syncNotification->setString(text);
        syncNotification->setIcon(icon);
        syncNotification->setTime(NOTIFICATION_LONG_TIME);
    }
    // The notification queue retains it through the completion animation.
    syncNotification=nullptr;
}

struct Prompt {
    aps::BatchReview review;
    arc::oneshot::Sender<bool> reply;
};
std::optional<Prompt> prompt;
class SyncConfirmation : public Popup {
    std::optional<arc::oneshot::Sender<bool>> reply;
    void answer(bool confirmed) {
        if(reply) {
            status=confirmed?"Sync All approved. Syncing your playback files; please wait.":"Sync cancelled. No files were uploaded or replaced.";
            log::info("Playback batch confirmation: {}",confirmed?"approved":"cancelled");
            if(confirmed) showLiveProgress("Attempt Sync: starting uploads...");
            (void)reply->send(confirmed); reply.reset();
        }
        Popup::onClose(nullptr);
    }
    void onClose(CCObject*) override { answer(false); }
    void onSync(CCObject*) { answer(true); }
    bool setup(aps::BatchReview const& review) {
        if(!Popup::init(380.f,290.f)) return false;
        setTitle("Review Playback Sync");
        auto row=[&](std::string const& text,float y,float scale=0.85f) {
            auto label=CCLabelBMFont::create(text.c_str(),"chatFont.fnt");
            label->setScale(scale);
            if(label->getContentSize().width*label->getScale()>340.f)
                label->setScale(340.f/label->getContentSize().width);
            label->setPosition({190.f,y});
            m_mainLayer->addChild(label);
        };
        row(fmt::format("{} files - one confirmation",review.files),233.f,1.f);
        row(fmt::format("Upload: {}   Update on device: {}",review.uploads,review.downloads),207.f);
        row(fmt::format("Local: {:.1f} MiB / {} attempts",double(review.localBytes)/(1024*1024),review.localAttempts),178.f);
        row(fmt::format("GitHub: {:.1f} MiB / {} attempts",double(review.cloudBytes)/(1024*1024),review.cloudAttempts),155.f);
        row(fmt::format("{} local-only / {} cloud-only attempts",review.localOnly,review.cloudOnly),132.f);
        row(fmt::format("Local file larger in {} of {} files",review.localLarger,review.files),109.f);
        row("Both histories kept. Replaced files backed up.",86.f,0.8f);
        row(review.uploads?"Uploads are public: anyone can download them.":"Download only: no playbacks will be published.",65.f,0.8f);
        auto button=[&](char const* text,float x,SEL_MenuHandler handler) {
            auto sprite=ButtonSprite::create(text,"goldFont.fnt","GJ_button_01.png",0.8f);
            sprite->setScale(0.85f);
            auto item=CCMenuItemSpriteExtra::create(sprite,this,handler);
            m_buttonMenu->addChildAtPosition(item,Anchor::Bottom,{x,29.f});
        };
        button("Cancel",-70.f,menu_selector(SyncConfirmation::onClose));
        button("Sync All",65.f,menu_selector(SyncConfirmation::onSync));
        return true;
    }
public:
    explicit SyncConfirmation(arc::oneshot::Sender<bool> sender):reply(std::move(sender)) {}
    static SyncConfirmation* create(Prompt value) {
        auto popup=new SyncConfirmation(std::move(value.reply));
        if(!popup->setup(value.review)) { delete popup; return nullptr; }
        popup->autorelease(); return popup;
    }
};
void showPrompt() {
    if(!prompt || GameManager::get()->m_playLayer || GameManager::get()->m_levelEditorLayer) return;
    auto value=std::move(*prompt); prompt.reset();
    if(auto popup=SyncConfirmation::create(std::move(value))) popup->show();
}
arc::Future<bool> confirmSync(aps::BatchReview review) {
    auto [sender,receiver]=arc::oneshot::channel<bool>();
    queueInMainThread([review,sender=std::move(sender)] mutable {
        prompt.emplace(Prompt{review,std::move(sender)});
        status="Waiting for Sync All confirmation. Return to the main menu to review.";
        showLiveProgress("Attempt Sync: awaiting approval");
        showPrompt();
    });
    auto result=co_await receiver.recv();
    co_return result.isOk() && result.unwrap();
}

// Worker progress is delivered to the main thread, just like confirmation UI.
void progress(std::string message,std::string banner) {
    queueInMainThread([message=std::move(message),banner=std::move(banner)] {
        if(!running) return; // A late progress delivery must not reopen a finished banner.
        status=message;
        showLiveProgress(banner);
        log::info("{}",message);
    });
}

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
       .header("User-Agent","AttemptSync/1.3.3")
       .timeout(std::chrono::seconds(45));
    return req;
}
void ok(WebResponse const& r) {
    if(!r.ok()) throw std::runtime_error("GitHub request failed (HTTP "+std::to_string(r.code())+"). Check connection, repository, branch and token permissions; retry later.");
}
matjson::Value json(WebResponse const& r) {
    ok(r); auto j=r.json(); aps::require(j.isOk(),"Invalid GitHub JSON"); return j.unwrap();
}
struct Remote { std::string sha; size_t size=0; };
// One tree request replaces a metadata request per level during compare.
arc::Future<std::map<std::string,Remote>> remoteFiles(Config const& c,std::string const& commit) {
    auto base=std::string("https://api.github.com/repos/")+aps::playbackRepository;
    auto response=co_await request(c).param("recursive","1").get(base+"/git/trees/"+commit);
    std::map<std::string,Remote> files;
    if(response.code()==404) co_return files;
    auto tree=json(response);
    aps::require(!tree["truncated"].asBool().unwrapOr(false),"GitHub repository tree is too large to list");
    auto const& items=tree["tree"]; aps::require(items.isArray(),"Unexpected GitHub tree response");
    constexpr std::string_view prefix="attempts/";
    for(auto const& item:items) {
        auto path=str(item,"path");
        if(!std::string_view(path).starts_with(prefix)) continue;
        auto name=path.substr(prefix.size());
        if(!aps::safeName(name) || str(item,"type")!="blob") continue;
        auto size=item["size"].asInt();
        aps::require(size.isOk() && size.unwrap()>=0 && size.unwrap()<=std::intmax_t(aps::maxBytes),"Remote attempt size invalid or exceeds 20 MiB");
        aps::require(files.size()<1000,"GitHub folder listing limit reached (1000 files)");
        files[name]={str(item,"sha"),size_t(size.unwrap())};
    }
    co_return files;
}

arc::Future<Outcome> sync(Config c) {
    Outcome out;
    size_t importBytes=0;
    std::string currentFile;
    aps::Cache cache;
    try {
        cache=aps::loadCache(c.cache);
        auto base=std::string("https://api.github.com/repos/")+aps::playbackRepository;
        // Resolve branch first: a missing repo/branch must not be mistaken for an empty folder.
        auto branchResponse=co_await request(c).get(base+"/branches/"+aps::playbackBranch);
        if(branchResponse.code()==404 && c.token.empty())
            throw std::runtime_error("gd-attempts is private. Add your own authorized GitHub token in settings to access it.");
        auto branch=json(branchResponse);
        auto commit=str(branch["commit"],"sha");
        auto cloud=co_await remoteFiles(c,commit);
        std::set<std::string> names;
        for(auto const& item:cloud) names.insert(item.first);
        fs::create_directories(c.dir);
        if(c.owner) for(auto const& entry:fs::directory_iterator(c.dir)) {
            auto name=entry.path().filename().string();
            if(entry.is_regular_file() && aps::safeName(name)) names.insert(name);
        }
        aps::require(names.size()<1000,"Too many level files to sync");
        struct Prepared { std::string name,sha; fs::path path; aps::Bytes local,merged; bool upload; };
        std::vector<Prepared> batch;
        aps::BatchReview review;
        size_t preparedBytes=0;
        // Prepare every file before asking once. No uploads or imports happen
        // until the complete, validated batch has been approved.
        struct Job {
            std::string name; fs::path path; aps::Bytes local; uint64_t hash=0;
            std::string sha; size_t size=0; bool cached=false,download=false;
        };
        std::vector<std::string> order(names.begin(),names.end());
        size_t reviewed=0,cachedCount=0;
        for(size_t start=0;start<order.size();) {
            // Fetch a small window of blobs concurrently. Small files fill the
            // window; large blobs shrink it so peak memory stays bounded.
            constexpr size_t maxParallel=8,maxWindowBytes=16*1024*1024;
            std::vector<Job> jobs;
            std::vector<WebFuture> futures;
            size_t end=start,windowBytes=0;
            while(end<order.size() && jobs.size()<maxParallel) {
                Job job; job.name=order[end]; job.path=c.dir/job.name; currentFile=job.name;
                job.local=read(job.path); job.hash=aps::fingerprint(job.local);
                auto found=cloud.find(job.name);
                if(found!=cloud.end()) {
                    aps::require(found->second.size>=8,"Remote attempt file is empty or truncated");
                    job.sha=found->second.sha; job.size=found->second.size;
                    auto entry=cache.find(job.name);
                    job.cached=entry!=cache.end() && entry->second.sha==job.sha && entry->second.hash==job.hash;
                    if(!job.cached) {
                        // Public viewers read blob bytes from the CDN so large
                        // first syncs do not exhaust the 60-request API quota.
                        auto blobUrl=c.token.empty()
                            ? "https://raw.githubusercontent.com/"+std::string(aps::playbackRepository)+"/"+commit+"/attempts/"+job.name
                            : base+"/git/blobs/"+job.sha;
                        futures.push_back(request(c,true).get(blobUrl));
                        job.download=true; windowBytes+=job.size;
                    }
                }
                jobs.push_back(std::move(job));
                ++end;
                if(windowBytes>=maxWindowBytes) break;
            }
            auto responses=co_await arc::joinAll(std::move(futures));
            size_t next=0;
            for(auto& job:jobs) {
                currentFile=job.name;
                ++reviewed;
                progress(fmt::format("Comparing file {} of {}: {}. Please wait; the batch review will appear when ready.",reviewed,order.size(),job.name),fmt::format("Comparing {} / {}",reviewed,order.size()));
                if(job.cached) { ++out.synced; ++cachedCount; continue; }
                aps::Bytes bytes;
                if(job.download) {
                    auto& blob=responses[next++];
                    ok(blob); bytes=std::move(blob).data();
                    aps::require(bytes.size()==job.size,"Remote blob size mismatch");
                    aps::require(bytes.size()>=8,"Remote attempt file is empty or truncated");
                }
                aps::require(c.owner || !bytes.empty(),"Playback missing from the selected cloud snapshot");
                auto plan=aps::planSync(bytes,job.local,c.owner);
                if(plan.upload || plan.imported!=job.local) {
                    review.add(aps::compareFiles(bytes,job.local),plan.upload,plan.imported!=job.local);
                    preparedBytes+=job.local.size()+plan.imported.size();
                    aps::require(preparedBytes<=128*1024*1024,"Sync batch exceeds 128 MiB. No files were changed.");
                    batch.push_back({job.name,job.sha,job.path,std::move(job.local),std::move(plan.imported),plan.upload});
                    cache.erase(job.name);
                } else {
                    ++out.synced;
                    if(!job.sha.empty()) cache[job.name]={job.sha,job.hash};
                }
            }
            start=end;
        }
        aps::saveCache(c.cache,cache);
        if(cachedCount) log::info("Playback sync: {} unchanged level files skipped",cachedCount);
        if(c.owner && review.files && !(co_await confirmSync(review))) {
            out.cancelled=true;
            out.message="Sync cancelled. No files were uploaded or replaced.";
            co_return out;
        }
        size_t processed=0;
        for(auto& item:batch) {
            currentFile=item.name;
            ++processed;
            progress(fmt::format("{} file {} of {}: {}. Please wait; no more confirmation is needed.",c.owner?"Syncing":"Downloading",processed,batch.size(),item.name),fmt::format("Syncing {} / {}",processed,batch.size()));
            auto endpoint=base+"/contents/attempts/"+item.name;
            bool done=false;
            for(int retry=0;retry<3 && !done;++retry) {
                if(item.upload) {
                    progress(fmt::format("Uploading file {} of {}: {}.",processed,batch.size(),item.name),
                             fmt::format("Uploading {} / {} ({}%)",processed,batch.size(),(processed-1)*100/batch.size()));
                    auto body=matjson::Value::object();
                    body["message"]="Sync Attempt Playback: "+item.name;
                    body["branch"]=aps::playbackBranch;
                    body["content"]=aps::base64(item.merged);
                    if(!item.sha.empty()) body["sha"]=item.sha;
                    auto uploaded=co_await request(c).bodyJSON(body).put(endpoint);
                    if(uploaded.code()==409 || uploaded.code()==422) {
                        progress(fmt::format("Retrying file {} of {} after a cloud change: {}.",processed,batch.size(),item.name),fmt::format("Retrying upload {} / {}",processed,batch.size()));
                        // Retain the approved local snapshot, merge cloud additions,
                        // and retry without another confirmation for this file.
                        auto response=co_await request(c).param("ref",aps::playbackBranch).get(endpoint);
                        aps::Bytes remote;
                        item.sha.clear();
                        if(response.code()!=404) {
                            auto meta=json(response);
                            aps::require(str(meta,"type")=="file","Remote attempt path is not a file");
                            auto size=meta["size"].asInt();
                            aps::require(size.isOk() && size.unwrap()>=8 && size.unwrap()<=int64_t(aps::maxBytes),"Remote attempt size invalid");
                            item.sha=str(meta,"sha");
                            auto blob=co_await request(c,true).get(base+"/git/blobs/"+item.sha);
                            ok(blob); remote=blob.data();
                            aps::require(remote.size()==size_t(size.unwrap()),"Remote blob size mismatch");
                        }
                        auto plan=aps::planSync(remote,item.merged,true);
                        preparedBytes-=item.merged.size();
                        preparedBytes+=plan.imported.size();
                        aps::require(preparedBytes<=128*1024*1024,"Sync batch exceeds 128 MiB after cloud changes. Retry sync.");
                        item.merged=std::move(plan.imported); item.upload=plan.upload;
                        continue;
                    }
                    ok(uploaded);
                    // The write response carries the new blob SHA, so the next
                    // sync can recognize this level without downloading it.
                    if(auto body=uploaded.json(); body.isOk()) {
                        auto sha=body.unwrap()["content"]["sha"].asString();
                        if(sha.isOk()) cache[item.name]={sha.unwrap(),aps::fingerprint(item.merged)};
                    }
                }
                // No upload means `item.sha` still identifies the cloud blob this
                // merged file was built from; remember it to skip later downloads.
                if(!item.upload && !item.sha.empty()) cache[item.name]={item.sha,aps::fingerprint(item.merged)};
                if(item.merged!=item.local) {
                    importBytes+=item.local.size()+item.merged.size();
                    aps::require(importBytes<=128*1024*1024,"Download batch exceeds 128 MiB. Retry sync.");
                    out.imports.push_back({item.path,std::move(item.local),std::move(item.merged)});
                }
                progress(fmt::format("Completed file {} of {}: {}.",processed,batch.size(),item.name),
                         fmt::format("Synced {} / {} ({}%)",processed,batch.size(),processed*100/batch.size()));
                log::info("Completed playback file {} of {}: {}",processed,batch.size(),item.name);
                ++out.synced; done=true;
            }
            aps::require(done,"Another device kept changing this file. Retry sync.");
        }
        out.message=(c.owner?"Synced ":"Downloaded playbacks for ")+std::to_string(out.synced)+" levels.";
        if(!c.owner && out.synced==0) out.message="No playbacks published yet. The owner needs to enable Upload My Attempts and sync first.";
        aps::saveCache(c.cache,cache);
    } catch(std::exception const& e) {
        aps::saveCache(c.cache,cache);
        out.failed=true; out.message=(currentFile.empty()?std::string():currentFile+": ")+e.what();
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
            showLiveProgress(fmt::format("Applying {} / {}", &item-out.imports.data()+1,out.imports.size()));
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
    finishProgress(out.failed?"Attempt Sync needs attention":out.cancelled?"Attempt Sync cancelled":
                   fmt::format("Attempt Sync finished: {} levels",out.synced),
                   (out.failed || skipped)?NotificationIcon::Warning:out.cancelled?NotificationIcon::Info:NotificationIcon::Success);
}
void start() {
    installPending();
    showPrompt();
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
        c.cache=mod->getSaveDir()/"sync-cache.txt";
        running=true; status="Syncing with GitHub. You can keep playing; downloads apply at the main menu.";
        showLiveProgress("Attempt Sync: connecting...");
        async::spawn(sync(std::move(c)),[](Outcome out) {
            pending=std::move(out); status="Downloads ready; return to the main menu to apply them.";
            showLiveProgress("Ready: return to menu to apply"); installPending();
        });
    } catch(std::exception const& e) { status=e.what(); FLAlertLayer::create("Attempt Sync",status,"OK")->show(); }
}
}
class $modify(SyncMenu,MenuLayer) {
    bool init() {
        if(!MenuLayer::init()) return false;
        installPending();
        showPrompt();
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
