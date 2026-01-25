#pragma once

#include "interfaces/i_process_data_provider.hpp"
#include "interfaces/i_system_data_provider.hpp"
#include "interfaces/i_process_killer.hpp"
#include <memory>

namespace pex {

// Factory functions to create platform-specific providers.
// Implemented per-platform; current build provides Linux implementations.
std::unique_ptr<IProcessDataProvider> make_process_data_provider();
std::unique_ptr<IProcessDataProvider> make_details_data_provider(); // separate instance if needed
std::unique_ptr<ISystemDataProvider> make_system_data_provider();
std::unique_ptr<IProcessKiller> make_process_killer();

} // namespace pex
