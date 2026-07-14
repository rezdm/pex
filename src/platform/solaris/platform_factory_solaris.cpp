#include "../platform_factory.hpp"
#include "solaris_process_data_provider.hpp"
#include "solaris_system_data_provider.hpp"
#include "solaris_process_killer.hpp"
#include "solaris_process_event_source.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<SolarisProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    return std::make_unique<SolarisProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<SolarisSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<SolarisProcessKiller>();
}

std::unique_ptr<IProcessEventSource> make_process_event_source() {
    return std::make_unique<SolarisProcessEventSource>();
}

} // namespace pex
