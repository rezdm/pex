#pragma once

#include "../interfaces/i_process_killer.hpp"
#include <vector>
#include <map>
#include <cstdint>

namespace pex {

class LinuxProcessKiller : public IProcessKiller {
public:
    LinuxProcessKiller() = default;
    ~LinuxProcessKiller() override = default;

    [[nodiscard]] std::optional<uint64_t> process_start_token(int pid) override;
    KillResult kill_process(int pid, bool force,
                            std::optional<uint64_t> expected_token) override;
    KillResult kill_process_tree(int pid, bool force,
                                 std::optional<uint64_t> expected_token) override;

private:
    struct ProcMeta {
        int ppid = -1;
        uint64_t starttime = 0;
    };

    static std::string get_kill_error_message(int err);
    static void collect_descendants_from_proc(int root_pid,
                                              std::vector<int>& result,
                                              std::map<int, std::vector<int>>& children_map,
                                              std::map<int, uint64_t>& start_times);
    static int get_ppid(int pid);
    static bool read_proc_meta(int pid, ProcMeta& out);
    static bool is_same_process(int pid, uint64_t starttime);
};

} // namespace pex
