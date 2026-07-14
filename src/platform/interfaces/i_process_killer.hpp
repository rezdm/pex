#pragma once

#include <cstdint>
#include <optional>
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

    // Opaque token identifying a live process *instance* (platform-packed
    // start time). Capture it when a kill is requested (e.g. when the
    // confirmation dialog opens); the kill methods use it to refuse signaling
    // a PID that has since been recycled by an unrelated process.
    // Returns nullopt if the process does not exist or is not readable.
    [[nodiscard]] virtual std::optional<uint64_t> process_start_token(int pid) = 0;

    // expected_token: when set, the PID is verified to still refer to the
    // same process instance immediately before signaling; on mismatch the
    // kill is aborted with an explanatory error.
    virtual KillResult kill_process(int pid, bool force,
                                    std::optional<uint64_t> expected_token = std::nullopt) = 0;
    virtual KillResult kill_process_tree(int pid, bool force,
                                         std::optional<uint64_t> expected_token = std::nullopt) = 0;
};

} // namespace pex
