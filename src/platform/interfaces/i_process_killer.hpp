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

// Shared PID-recycle guard for every killer implementation: returns a failure
// KillResult if 'pid' no longer refers to the process instance identified by
// 'token' (gone or recycled), or nullopt when it is safe to signal. Keeping
// this in one place stops the per-platform copies (and their user-facing
// messages) from drifting apart.
[[nodiscard]] inline std::optional<KillResult> check_kill_token(
    IProcessKiller& killer, const int pid, const std::optional<uint64_t>& token) {
    if (!token) return std::nullopt;
    KillResult result;
    const auto current = killer.process_start_token(pid);
    if (!current) {
        result.error_message = "Process not found. It may have already terminated.";
        return result;
    }
    if (*current != *token) {
        result.error_message =
            "PID was reused by a different process since the dialog opened. Kill aborted.";
        return result;
    }
    return std::nullopt;
}

} // namespace pex
