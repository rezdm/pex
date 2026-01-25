#pragma once

#include "../i_view.hpp"

namespace pex {

// Simple view interface for process list/tree
// ImGui immediate mode handles interactions inline via ViewModels
class IProcessListView : public IView {
public:
    // No callbacks - state changes are made directly to ViewModels
};

} // namespace pex
