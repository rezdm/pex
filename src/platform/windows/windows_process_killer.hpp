#pragma once

#include "../interfaces/i_process_killer.hpp"

namespace pex {

// Windows process killer (issue #43). Windows has no SIGTERM equivalent;
// "graceful" and "force" both use TerminateProcess. The tree variant
// guards against PID reuse by comparing process creation times.
class WindowsProcessKiller final : public IProcessKiller {
public:
    [[nodiscard]] std::optional<uint64_t> process_start_token(int pid) override;
    KillResult kill_process(int pid, bool force,
                            std::optional<uint64_t> expected_token) override;
    KillResult kill_process_tree(int pid, bool force,
                                 std::optional<uint64_t> expected_token) override;
};

} // namespace pex
