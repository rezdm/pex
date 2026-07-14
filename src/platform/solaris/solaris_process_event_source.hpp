#pragma once

#include "../interfaces/i_process_event_source.hpp"

namespace pex {

// Inactive stub: Solaris has no practical system-wide process event feed
// for an unprivileged observer. Contract events (libcontract) only deliver
// the event types a contract was *created* to request, and the process
// contracts set up by init/SMF carry empty informative event sets — so the
// /system/contract/process bundle stays silent for almost everything.
// System-wide observation would effectively require a dtrace consumer
// (proc:::create / proc:::exit), which is out of scope for a monitor that
// must run unprivileged. pex runs poll-only on Solaris.
class SolarisProcessEventSource : public IProcessEventSource {
public:
    bool start() override { return false; }
    void stop() override {}
    [[nodiscard]] bool is_active() const override { return false; }
    std::vector<ProcessEvent> drain() override { return {}; }
};

} // namespace pex
