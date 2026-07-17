#pragma once

#include "../model/process_info.hpp"

#include <memory>
#include <unordered_set>
#include <vector>

namespace pex {

struct DataSnapshot;

// Result of comparing two consecutive snapshots (issue #61, Process
// Explorer-style difference highlighting): which PIDs are new, and the
// processes that disappeared (for red "ghost" rows).
struct SnapshotDiff {
    std::unordered_set<int> new_pids;
    // Point into 'previous_snapshot' (owned by it) — no per-tick ProcessInfo
    // string copies; kept alive by the shared_ptr below.
    std::vector<const ProcessInfo*> exited_processes;
    std::shared_ptr<const DataSnapshot> previous_snapshot;
};

// Either argument may be null: with no previous snapshot nothing is "new"
// (the first tick must not flash every process green), and with no current
// snapshot the diff is empty. 'previous' is held by the result so the
// exited-process pointers stay valid for the diff's lifetime.
[[nodiscard]] SnapshotDiff compute_snapshot_diff(std::shared_ptr<const DataSnapshot> previous,
                                                 const DataSnapshot* current);

} // namespace pex
