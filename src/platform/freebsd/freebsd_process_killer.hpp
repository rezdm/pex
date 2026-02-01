#pragma once

#include "../interfaces/i_process_killer.hpp"

namespace pex {

class FreeBSDProcessKiller : public IProcessKiller {
public:
    KillResult kill_process(int pid, bool force) override;
    KillResult kill_process_tree(int pid, bool force) override;
};

} // namespace pex
