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
