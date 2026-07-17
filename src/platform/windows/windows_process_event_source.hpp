#pragma once

#include "../interfaces/i_process_event_source.hpp"

namespace pex {

// Inactive stub (issue #43, #60). Windows can deliver process create/exit
// notifications (WMI __InstanceCreationEvent / __InstanceDeletionEvent, or
// the Microsoft-Windows-Kernel-Process ETW provider), but wiring that up is
// out of scope for the current TUI port. pex runs poll-only on Windows.
class WindowsProcessEventSource : public IProcessEventSource {
public:
    bool start() override { return false; }
    void stop() override {}
    [[nodiscard]] bool is_active() const override { return false; }
    std::vector<ProcessEvent> drain() override { return {}; }
};

} // namespace pex
