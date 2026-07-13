#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace pex {

struct KillDialogViewModel {
    // Visibility
    bool is_visible = false;

    // Target process
    int target_pid = -1;
    std::string target_name;
    // Start-time token captured when the dialog opened; the killer refuses to
    // signal the PID if it has been recycled since (see IProcessKiller).
    std::optional<uint64_t> target_token;

    // Kill mode
    bool is_tree_kill = false;

    // Error state
    std::string error_message;
    bool show_force_option = false;  // Show force kill after SIGTERM fails
};

} // namespace pex
