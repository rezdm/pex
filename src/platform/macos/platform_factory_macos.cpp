#include "../platform_factory.hpp"
#include "macos_process_data_provider.hpp"
#include "macos_system_data_provider.hpp"
#include "macos_process_killer.hpp"
#include "macos_process_event_source.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<MacosProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    return std::make_unique<MacosProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<MacosSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<MacosProcessKiller>();
}

std::unique_ptr<IProcessEventSource> make_process_event_source() {
    return std::make_unique<MacosProcessEventSource>();
}

} // namespace pex
