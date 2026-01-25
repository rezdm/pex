#include "../platform_factory.hpp"
#include "freebsd_process_data_provider.hpp"
#include "freebsd_system_data_provider.hpp"
#include "freebsd_process_killer.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<FreeBSDProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    return std::make_unique<FreeBSDProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<FreeBSDSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<FreeBSDProcessKiller>();
}

} // namespace pex
