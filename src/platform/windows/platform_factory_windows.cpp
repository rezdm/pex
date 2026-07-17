#include "../platform_factory.hpp"
#include "windows_process_data_provider.hpp"
#include "windows_system_data_provider.hpp"
#include "windows_process_killer.hpp"
#include "windows_process_event_source.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<WindowsProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    return std::make_unique<WindowsProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<WindowsSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<WindowsProcessKiller>();
}

std::unique_ptr<IProcessEventSource> make_process_event_source() {
    return std::make_unique<WindowsProcessEventSource>();
}

} // namespace pex
