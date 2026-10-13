#pragma once
#include "apx.hpp"
#include <utility>

namespace aps {
inline constexpr char playbackRepository[] = "hdlolhfff/gd-attempts";
inline constexpr char playbackBranch[] = "main";

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
