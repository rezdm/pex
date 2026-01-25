#include "../platform_factory.hpp"

#include "linux_process_data_provider.hpp"
#include "linux_system_data_provider.hpp"
#include "linux_process_killer.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<LinuxProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    // Separate instance to avoid sharing state across UI/detail threads.
    return std::make_unique<LinuxProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<LinuxSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<LinuxProcessKiller>();
}

} // namespace pex
