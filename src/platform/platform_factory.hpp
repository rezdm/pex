#pragma once

#include "interfaces/i_process_data_provider.hpp"
#include "interfaces/i_system_data_provider.hpp"
#include "interfaces/i_process_killer.hpp"
#include "interfaces/i_process_event_source.hpp"
#include <memory>

namespace pex {

// Factory functions to create platform-specific providers.
// Implemented per-platform; current build provides Linux implementations.
std::unique_ptr<IProcessDataProvider> make_process_data_provider();
std::unique_ptr<IProcessDataProvider> make_details_data_provider(); // separate instance if needed
std::unique_ptr<ISystemDataProvider> make_system_data_provider();
std::unique_ptr<IProcessKiller> make_process_killer();
// Kernel process-event feed (issue #60). Always returns an object; call
// start() to find out whether the mechanism is actually available.
std::unique_ptr<IProcessEventSource> make_process_event_source();

} // namespace pex
