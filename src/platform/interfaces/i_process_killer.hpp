#pragma once

#include <string>

namespace pex {

struct KillResult {
    bool success = false;
    bool process_still_running = false;
    std::string error_message;
};

class IProcessKiller {
public:
    virtual ~IProcessKiller() = default;

    virtual KillResult kill_process(int pid, bool force) = 0;
    virtual KillResult kill_process_tree(int pid, bool force) = 0;
};

} // namespace pex
