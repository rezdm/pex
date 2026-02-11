#include "../platform_factory.hpp"
#include "openvms_process_data_provider.hpp"
#include "openvms_system_data_provider.hpp"
#include "openvms_process_killer.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<OpenVMSProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    return std::make_unique<OpenVMSProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<OpenVMSSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<OpenVMSProcessKiller>();
}

} // namespace pex
