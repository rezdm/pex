#pragma once

#include "../interfaces/i_process_killer.hpp"
#include <vector>

namespace pex {

class LinuxProcessKiller : public IProcessKiller {
public:
    LinuxProcessKiller() = default;
    ~LinuxProcessKiller() override = default;

    KillResult kill_process(int pid, bool force) override;
    KillResult kill_process_tree(int pid, bool force) override;

private:
    static std::string get_kill_error_message(int err);
    static void collect_descendants_from_proc(int root_pid, std::vector<int>& result);
    static int get_ppid(int pid);
};

} // namespace pex
