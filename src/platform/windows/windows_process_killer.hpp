#pragma once

#include "../interfaces/i_process_killer.hpp"

namespace pex {

// Windows process killer (issue #43). Windows has no SIGTERM equivalent;
// "graceful" and "force" both use TerminateProcess. The tree variant
// guards against PID reuse by comparing process creation times.
class WindowsProcessKiller final : public IProcessKiller {
public:
    KillResult kill_process(int pid, bool force) override;
    KillResult kill_process_tree(int pid, bool force) override;
};

} // namespace pex
