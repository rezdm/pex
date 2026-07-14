#pragma once

#include "../model/process_info.hpp"

#include <unordered_set>
#include <vector>

namespace pex {

struct DataSnapshot;

// Result of comparing two consecutive snapshots (issue #61, Process
// Explorer-style difference highlighting): which PIDs are new, and copies of
// the processes that disappeared (for red "ghost" rows — the originals die
// with the old snapshot).
struct SnapshotDiff {
    std::unordered_set<int> new_pids;
    std::vector<ProcessInfo> exited_processes;
};

// Either pointer may be null: with no previous snapshot nothing is "new"
// (the first tick must not flash every process green), and with no current
// snapshot the diff is empty.
[[nodiscard]] SnapshotDiff compute_snapshot_diff(const DataSnapshot* previous,
                                                 const DataSnapshot* current);

} // namespace pex
