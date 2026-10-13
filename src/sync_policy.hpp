#pragma once
#include "apx.hpp"
#include <utility>
#include <set>

namespace aps {
inline constexpr char playbackRepository[] = "hdlolhfff/gd-attempts";
inline constexpr char playbackBranch[] = "main";

struct FileComparison {
    size_t localBytes, cloudBytes, localAttempts, cloudAttempts, localOnly, cloudOnly;
};
inline FileComparison compareFiles(Bytes const& remote, Bytes const& local) {
    auto cloud=parse(remote), device=parse(local);
    auto keys=[](File const& f) {
        std::set<Bytes> result;
        for(auto a:f.attempts) { put(a,0,0); a[13]=0; result.insert(std::move(a)); }
        return result;
    };
    auto cloudKeys=keys(cloud), localKeys=keys(device);
    size_t localOnly=0, cloudOnly=0;
    for(auto const& a:localKeys) if(!cloudKeys.contains(a)) ++localOnly;
    for(auto const& a:cloudKeys) if(!localKeys.contains(a)) ++cloudOnly;
    return {local.size(),remote.size(),device.attempts.size(),cloud.attempts.size(),localOnly,cloudOnly};
}

// A single review covers every changed file in this sync job.
struct BatchReview {
    size_t files=0,uploads=0,downloads=0,localLarger=0;
    size_t localBytes=0,cloudBytes=0,localAttempts=0,cloudAttempts=0,localOnly=0,cloudOnly=0;
    void add(FileComparison const& f,bool upload,bool download) {
        ++files; uploads+=upload; downloads+=download;
        localLarger+=f.localBytes>f.cloudBytes;
        localBytes+=f.localBytes; cloudBytes+=f.cloudBytes;
        localAttempts+=f.localAttempts; cloudAttempts+=f.cloudAttempts;
        localOnly+=f.localOnly; cloudOnly+=f.cloudOnly;
    }
};

struct SyncPlan {
    Bytes imported;
    bool upload;
};
// Viewers can import published ghosts while keeping their local recordings.
// A token grants access; the explicit owner switch alone enables uploads.
inline SyncPlan planSync(Bytes const& remote, Bytes const& local, bool owner) {
    auto merged = merge(remote, local);
    bool upload = owner && merged != remote;
    return {std::move(merged), upload};
}
}
