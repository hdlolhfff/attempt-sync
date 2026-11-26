#include <Geode/Geode.hpp>
#include <asp/fs/std.hpp>
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
#include "chunked_apx.hpp"
#include "sync_cache.hpp"
#include "sync_policy.hpp"

// CI supplies this from a repository secret. Local builds without it ship
// viewer-only behaviour.
#ifndef ATTEMPT_SYNC_SHARED_TOKEN
#define ATTEMPT_SYNC_SHARED_TOKEN ""
#endif

using namespace geode::prelude;
namespace fs = std::filesystem;
using geode::utils::web::WebRequest;
using geode::utils::web::WebResponse;
using geode::utils::web::WebFuture;

namespace {
struct Import { fs::path path; aps::Bytes before, after; };
struct Outcome { std::vector<Import> imports; std::string message; bool failed=false; bool cancelled=false; int synced=0; int skipped=0; };
struct Config { std::string token; fs::path dir, cache; bool owner=false; };
bool running=false;
std::optional<Outcome> pending;
std::string status="Playbacks sync with hdlolhfff/gd-attempts. Tap Sync.";

// Install a build's shared writer token once, so a fresh download can both
// read and write without any setup. A device that already holds a token keeps
// it, and turning Upload My Attempts off still makes that device read-only.
void applySharedDefaults() {
    if(std::string_view(ATTEMPT_SYNC_SHARED_TOKEN).empty()) return;
    auto mod=Mod::get();
    if(!mod->getSettingValue<std::string>("github-token").empty()) return;
    mod->setSettingValue<std::string>("github-token",ATTEMPT_SYNC_SHARED_TOKEN);
    mod->setSettingValue<bool>("upload-my-attempts",true);
    log::info("Attempt Sync: applied the built-in writer defaults");
}

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
    auto n=fs::file_size(p); aps::require(n>=8,"Attempt file is empty or truncated"); aps::checkAPXSize(n);
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
       .header("User-Agent","AttemptSync/1.5.0")
       .timeout(asp::Duration::fromSecs(45));
    return req;
}
void ok(WebResponse const& r) {
    if(!r.ok()) throw std::runtime_error("GitHub request failed (HTTP "+std::to_string(r.code())+"). Check connection, repository, branch and token permissions; retry later.");
}
matjson::Value json(WebResponse const& r) {
    ok(r); auto j=r.json(); aps::require(j.isOk(),"Invalid GitHub JSON"); return j.unwrap();
}
struct BranchHead { std::string commit, tree; };
arc::Future<BranchHead> branchHead(Config const& c, std::string const& base) {
    auto response = co_await request(c).get(base + "/branches/" + aps::playbackBranch);
    if (response.code() == 404 && c.token.empty())
        throw std::runtime_error("gd-attempts is private. Add your own authorized GitHub token in settings to access it.");
    if (response.code() == 404)
        throw std::runtime_error("gd-attempts or its main branch was not found. The saved token may be expired or no longer have access to that repository.");
    if (response.code() == 403)
        throw std::runtime_error("GitHub refused access (HTTP 403). The token may be rate limited, expired, or not authorized for gd-attempts. Replace it in settings and retry.");
    auto branch = json(response);
    co_return BranchHead{str(branch["commit"], "sha"), str(branch["commit"]["commit"]["tree"], "sha")};
}

struct Remote {
    std::string sha;
    size_t size = 0;
    aps::RemoteParts parts;
};
// One tree request replaces a metadata request per level during compare.
arc::Future<std::map<std::string, Remote>> remoteFiles(Config const& c, BranchHead const& head) {
    auto base=std::string("https://api.github.com/repos/")+aps::playbackRepository;
    auto response=co_await request(c).param("recursive","1").get(base+"/git/trees/"+head.tree);
    std::map<std::string,Remote> files;
    std::map<std::string, aps::RemoteParts> directories;
    auto tree=json(response);
    aps::require(!tree["truncated"].asBool().unwrapOr(false),"GitHub repository tree is too large to list");
    auto const& items=tree["tree"]; aps::require(items.isArray(),"Unexpected GitHub tree response");
    constexpr std::string_view prefix="attempts/";
    for(auto const& item:items) {
        auto path=str(item,"path");
        if(!std::string_view(path).starts_with(prefix)) continue;
        auto relative = path.substr(prefix.size());
        auto slash = relative.find('/');
        auto name = relative.substr(0, slash);
        if (!aps::safeName(name)) continue;
        auto type = str(item, "type");
        if (slash == std::string::npos && type == "tree") {
            files[name].sha = str(item, "sha");
            directories.try_emplace(name);
            continue;
        }
        auto mode = str(item, "mode");
        aps::require(type == "blob" && (mode == "100644" || (slash == std::string::npos && mode == "100755")),
            "Invalid remote APX tree entry");
        auto size=item["size"].asInt();
        aps::require(size.isOk() && size.unwrap() >= 0 && size.unwrap() <= std::intmax_t(aps::maxBytes), "Invalid remote APX size");
        if (slash == std::string::npos) {
            files[name] = {str(item, "sha"), size_t(size.unwrap()), {}};
        } else {
            directories[name].emplace(relative.substr(slash + 1), aps::RemotePart{str(item, "sha"), size_t(size.unwrap())});
        }
    }
    aps::require(files.size() < 1000, "GitHub folder listing limit reached (1000 files)");
    for (auto& [name, parts] : directories) {
        auto found = files.find(name);
        aps::require(found != files.end(), "Missing APX part directory");
        found->second.size = aps::chunkedSize(parts);
        found->second.parts = std::move(parts);
    }
    co_return files;
}

std::string blobUrl(Config const& c, std::string const& base, BranchHead const& head,
                    std::string const& path, std::string const& sha) {
    if (!c.token.empty()) return base + "/git/blobs/" + sha;
    return "https://raw.githubusercontent.com/" + std::string(aps::playbackRepository) +
        "/" + head.commit + "/attempts/" + path;
}

arc::Future<aps::Bytes> downloadAPX(Config const& c, std::string const& base, BranchHead const& head,
                                  std::string const& name, Remote const& file) {
    if (file.parts.empty()) {
        auto response = co_await request(c, true).get(blobUrl(c, base, head, name, file.sha));
        ok(response);
        auto bytes = std::move(response).data();
        aps::require(bytes.size() == file.size, "Remote blob size mismatch");
        co_return std::move(bytes);
    }
    aps::Bytes rebuilt;
    rebuilt.reserve(file.size);
    for (size_t index = 0; index < file.parts.size(); ++index) {
        progress(fmt::format("Downloading APX part {} of {} for {}.", index + 1, file.parts.size(), name),
                 fmt::format("Downloading part {} / {}", index + 1, file.parts.size()));
        auto partName = aps::partName(index, file.parts.size());
        auto const& part = file.parts.at(partName);
        auto response = co_await request(c, true).get(blobUrl(c, base, head, name + "/" + partName, part.sha));
        ok(response);
        aps::appendPart(rebuilt, std::move(response).data(), part.size);
    }
    co_return std::move(rebuilt);
}

void checkUpload(WebResponse const& response) {
    if (response.code() == 403 || response.code() == 404) {
        auto detail = response.code() == 403
            ? "the token may be rate limited, expired, or missing Contents: Read and write"
            : "the repository, branch, or this account's write access was not found";
        throw std::runtime_error("GitHub refused the upload (HTTP " + std::to_string(response.code()) + "): " +
            detail + ". The repository owner must add this GitHub account as a collaborator with Write access; then replace the token in settings.");
    }
    ok(response);
}

arc::Future<std::optional<std::string>> publishAPX(Config const& c, std::string const& base, BranchHead const& head,
                                                 std::string const& name, Remote const& old, aps::Bytes const& file,
                                                 std::string& stagedTree) {
    auto path = "attempts/" + name;
    auto message = "Sync Attempt Playback: " + name;
    if (file.size() <= aps::singleBlobLimit && old.parts.empty()) {
        auto body = matjson::Value::object();
        body["message"] = message;
        body["branch"] = aps::playbackBranch;
        body["content"] = aps::base64(file);
        if (!old.sha.empty()) body["sha"] = old.sha;
        auto response = co_await request(c).bodyJSON(body).put(base + "/contents/" + path);
        if (response.code() == 409 || response.code() == 422) co_return std::nullopt;
        checkUpload(response);
        co_return str(json(response)["content"], "sha");
    }

    if (stagedTree.empty()) {
        auto parts = aps::splitAPX(file);
        auto entries = matjson::Value::array();
        for (size_t index = 0; index < parts.size(); ++index) {
            progress(fmt::format("Uploading APX part {} of {} for {}.", index + 1, parts.size(), name),
                     fmt::format("Uploading part {} / {}", index + 1, parts.size()));
            auto body = matjson::Value::object();
            body["encoding"] = "base64";
            body["content"] = aps::base64(parts[index]);
            auto response = co_await request(c).bodyJSON(body).post(base + "/git/blobs");
            checkUpload(response);
            entries.push(matjson::makeObject({
                {"path", aps::partName(index, parts.size())}, {"mode", "100644"},
                {"type", "blob"}, {"sha", str(json(response), "sha")}
            }));
        }
        auto response = co_await request(c).bodyJSON(matjson::makeObject({{"tree", std::move(entries)}})).post(base + "/git/trees");
        checkUpload(response);
        stagedTree = str(json(response), "sha");
    }

    // Publish the whole part directory in one commit. A failed upload leaves the
    // branch on the old file, and a non-fast-forward update forces a fresh merge.
    auto entries = matjson::Value::array();
    entries.push(matjson::makeObject({{"path", path}, {"mode", "040000"}, {"type", "tree"}, {"sha", stagedTree}}));
    auto root = matjson::makeObject({{"base_tree", head.tree}, {"tree", std::move(entries)}});
    auto treeResponse = co_await request(c).bodyJSON(root).post(base + "/git/trees");
    checkUpload(treeResponse);
    auto parents = matjson::Value::array();
    parents.push(head.commit);
    auto commit = matjson::makeObject({
        {"message", message}, {"tree", str(json(treeResponse), "sha")}, {"parents", std::move(parents)}
    });
    auto commitResponse = co_await request(c).bodyJSON(commit).post(base + "/git/commits");
    checkUpload(commitResponse);
    auto ref = matjson::makeObject({{"sha", str(json(commitResponse), "sha")}, {"force", false}});
    auto updated = co_await request(c).bodyJSON(ref).patch(base + "/git/refs/heads/" + aps::playbackBranch);
    if (updated.code() == 409 || updated.code() == 422) co_return std::nullopt;
    checkUpload(updated);
    co_return stagedTree;
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
        auto head = co_await branchHead(c, base);
        auto cloud = co_await remoteFiles(c, head);
        std::set<std::string> names;
        for(auto const& item:cloud) names.insert(item.first);
        fs::create_directories(c.dir);
        if(c.owner) for(auto const& entry:fs::directory_iterator(c.dir)) {
            auto name=entry.path().filename().string();
            if(entry.is_regular_file() && aps::safeName(name)) names.insert(name);
        }
        aps::require(names.size()<1000,"Too many level files to sync");
        struct Prepared { std::string name; Remote remote; fs::path path; aps::Bytes local,merged; bool upload; };
        std::vector<Prepared> batch;
        aps::BatchReview review;
        size_t preparedBytes=0;
        size_t deferred=0;
        std::vector<std::string> oversized;
        // Prepare every file before asking once. No uploads or imports happen
        // until the complete, validated batch has been approved.
        struct Job {
            std::string name; fs::path path; aps::Bytes local; uint64_t hash=0;
            Remote const* remote = nullptr;
            bool cached = false;
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
                // A level past the per-file limit is left untouched instead of
                // aborting the batch, so one huge history cannot block every
                // other level.
                std::error_code sizeError;
                auto localSize=fs::file_size(job.path,sizeError);
                if(!sizeError && localSize>aps::maxBytes) {
                    oversized.push_back(job.name); cache.erase(job.name); ++end; continue;
                }
                job.local=read(job.path); job.hash=aps::fingerprint(job.local);
                windowBytes+=job.local.size();
                auto found=cloud.find(job.name);
                if(found!=cloud.end()) {
                    aps::require(found->second.size>=8,"Remote attempt file is empty or truncated");
                    job.remote=&found->second;
                    auto entry=cache.find(job.name);
                    job.cached=entry!=cache.end() && entry->second.sha==job.remote->sha && entry->second.hash==job.hash;
                    if(!job.cached) {
                        // Public viewers read blob bytes from the CDN so large
                        // first syncs do not exhaust the 60-request API quota.
                        if (job.remote->parts.empty()) {
                            futures.push_back(request(c,true).get(blobUrl(c,base,head,job.name,job.remote->sha)));
                        }
                        windowBytes+=job.remote->size;
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
                if(job.remote) {
                    if(job.remote->parts.empty()) {
                        auto& blob=responses[next++];
                        ok(blob); bytes=std::move(blob).data();
                        aps::require(bytes.size()==job.remote->size,"Remote blob size mismatch");
                    } else {
                        bytes=co_await downloadAPX(c,base,head,job.name,*job.remote);
                    }
                }
                aps::require(c.owner || !bytes.empty(),"Playback missing from the selected cloud snapshot");
                aps::SyncPlan plan;
                try {
                    plan=aps::planSync(bytes,job.local,c.owner);
                } catch (aps::APXTooLarge const&) {
                    oversized.push_back(job.name);
                    cache.erase(job.name);
                    continue;
                }
                if(plan.upload || plan.imported!=job.local) {
                    cache.erase(job.name);
                    auto retained=job.local.size()+plan.imported.size();
                    if(retained>aps::batchLimit-preparedBytes) {
                        ++deferred;
                        continue;
                    }
                    review.add(aps::compareFiles(bytes,job.local),plan.upload,plan.imported!=job.local);
                    preparedBytes+=retained;
                    batch.push_back({job.name,job.remote?*job.remote:Remote{},job.path,std::move(job.local),std::move(plan.imported),plan.upload});
                } else {
                    ++out.synced;
                    if(job.remote) cache[job.name]={job.remote->sha,job.hash};
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
            auto expectedHead=head;
            std::string stagedTree;
            bool done=false;
            // Several devices can write at once, so conflicts are expected.
            // Each retry re-reads the file, merges, and rewrites; the approved
            // local snapshot is never discarded.
            for(int retry=0;retry<6 && !done;++retry) {
                if(item.upload) {
                    progress(fmt::format("Uploading file {} of {}: {}.",processed,batch.size(),item.name),
                             fmt::format("Uploading {} / {} ({}%)",processed,batch.size(),(processed-1)*100/batch.size()));
                    auto published=co_await publishAPX(c,base,expectedHead,item.name,item.remote,item.merged,stagedTree);
                    if(!published) {
                        progress(fmt::format("Retrying file {} of {} after a cloud change: {}.",processed,batch.size(),item.name),fmt::format("Retrying upload {} / {}",processed,batch.size()));
                        expectedHead=co_await branchHead(c,base);
                        auto latest=co_await remoteFiles(c,expectedHead);
                        auto found=latest.find(item.name);
                        auto remote=found==latest.end()?Remote{}:std::move(found->second);
                        if(remote.sha!=item.remote.sha) {
                            aps::Bytes bytes;
                            if(!remote.sha.empty()) {
                                aps::require(remote.size>=8,"Remote attempt file is empty or truncated");
                                bytes=co_await downloadAPX(c,base,expectedHead,item.name,remote);
                            }
                            // Preserve the approved recordings when another device
                            // adds recordings before publication.
                            aps::SyncPlan plan;
                            try {
                                plan=aps::planSync(bytes,item.merged,true);
                            } catch (aps::APXTooLarge const&) {
                                oversized.push_back(item.name);
                                done=true;
                                continue;
                            }
                            preparedBytes-=item.merged.size();
                            preparedBytes+=plan.imported.size();
                            aps::require(preparedBytes<=aps::batchLimit,"Sync batch exceeds 512 MiB after cloud changes. Retry sync.");
                            if(plan.imported!=item.merged) stagedTree.clear();
                            item.merged=std::move(plan.imported);
                            item.upload=plan.upload;
                        }
                        item.remote=std::move(remote);
                        continue;
                    }
                    item.remote.sha=std::move(*published);
                }
                if(!item.remote.sha.empty()) cache[item.name]={item.remote.sha,aps::fingerprint(item.merged)};
                if(item.merged!=item.local) {
                    importBytes+=item.local.size()+item.merged.size();
                    aps::require(importBytes<=aps::batchLimit,"Download batch exceeds 512 MiB. Retry sync.");
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
        if(!oversized.empty()) {
            std::string list;
            for(size_t i=0;i<oversized.size() && i<3;++i) list+=std::string(i?", ":"")+oversized[i];
            if(oversized.size()>3) list+=", and more";
            out.message+=" Skipped "+std::to_string(oversized.size())+" level(s) over 256 MiB ("+list+"). Those files were left unchanged.";
        }
        if(deferred) out.message+=" Sync again to process "+std::to_string(deferred)+" level(s) deferred to fit the 512 MiB batch limit.";
        out.skipped=int(oversized.size()+deferred);
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
        auto root=(Mod::get()->getSaveDir()/"backups").toStdPath().unwrap();
        for(auto const& item:out.imports) {
            try {
                if(read(item.path)!=item.before) { ++skipped; continue; }
            } catch (aps::APXTooLarge const&) {
                ++skipped;
                continue;
            }
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
        if(out.failed) status+=". Completed files were kept; retry to finish.";
    } catch(std::exception const& e) { out.failed=true; status=std::string("Import failed: ")+e.what()+". Original files are in backups."; }
    running=false;
    if(out.failed || out.skipped) FLAlertLayer::create("Attempt Sync",status,"OK")->show();
    finishProgress(out.failed?"Attempt Sync needs attention":out.cancelled?"Attempt Sync cancelled":
                   out.skipped?fmt::format("Attempt Sync: {} levels synced, {} skipped",out.synced,out.skipped):
                   fmt::format("Attempt Sync finished: {} levels",out.synced),
                   (out.failed || skipped || out.skipped)?NotificationIcon::Warning:out.cancelled?NotificationIcon::Info:NotificationIcon::Success);
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
        aps::require(playback!=nullptr,"Enable Attempt Playback 1.6.0-beta.1 or 1.6.0 first.");
        constexpr auto testedBeta=VersionInfo(1,6,0,VersionTag(VersionTag::Beta,1));
        constexpr auto stable=VersionInfo(1,6,0);
        auto version=playback->getVersion();
        aps::require(version==testedBeta || version==stable,"Use Attempt Playback 1.6.0-beta.1 or 1.6.0 to sync recordings.");
        c.dir=(playback->getSaveDir()/"attempts").toStdPath().unwrap();
        c.cache=(mod->getSaveDir()/"sync-cache.txt").toStdPath().unwrap();
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
        applySharedDefaults();
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
