#pragma once

#include "../interfaces/i_process_killer.hpp"

namespace pex {

class FreeBSDProcessKiller : public IProcessKiller {
public:
    [[nodiscard]] std::optional<uint64_t> process_start_token(int pid) override;
    KillResult kill_process(int pid, bool force,
                            std::optional<uint64_t> expected_token) override;
    KillResult kill_process_tree(int pid, bool force,
                                 std::optional<uint64_t> expected_token) override;
};

} // namespace pex
