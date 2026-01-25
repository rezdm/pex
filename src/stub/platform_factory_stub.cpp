#include "../platform_factory.hpp"
#include "stub_providers.hpp"

namespace pex {

std::unique_ptr<IProcessDataProvider> make_process_data_provider() {
    return std::make_unique<StubProcessDataProvider>();
}

std::unique_ptr<IProcessDataProvider> make_details_data_provider() {
    return std::make_unique<StubProcessDataProvider>();
}

std::unique_ptr<ISystemDataProvider> make_system_data_provider() {
    return std::make_unique<StubSystemDataProvider>();
}

std::unique_ptr<IProcessKiller> make_process_killer() {
    return std::make_unique<StubProcessKiller>();
}

} // namespace pex
