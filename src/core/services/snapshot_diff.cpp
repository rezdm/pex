#include "snapshot_diff.hpp"
#include "data_store.hpp"

#include <algorithm>
#include <utility>

namespace pex {

SnapshotDiff compute_snapshot_diff(std::shared_ptr<const DataSnapshot> previous,
                                   const DataSnapshot* current) {
    SnapshotDiff diff;
    if (!previous || !current) return diff;

    // DataStore publishes a non-null but empty snapshot from its constructor,
    // before the first /proc scan. Diffing the first populated snapshot
    // against it would mark every process "new" and flash the whole table
    // green on launch; an empty baseline is treated as "no baseline".
    if (previous->process_map.empty()) return diff;

    for (const auto& [pid, node] : current->process_map) {
        if (!previous->process_map.contains(pid)) {
            diff.new_pids.insert(pid);
        }
    }

    for (const auto& [pid, node] : previous->process_map) {
        if (!current->process_map.contains(pid)) {
            diff.exited_processes.push_back(&node->info);
        }
    }
    // process_map iteration order is unstable; keep ghost rows deterministic
    std::ranges::sort(diff.exited_processes,
                      [](const ProcessInfo* a, const ProcessInfo* b) { return a->pid < b->pid; });

    diff.previous_snapshot = std::move(previous);
    return diff;
}

} // namespace pex
