#pragma once

#include "../i_view.hpp"

namespace pex {

// Simple view interface for process popup/details window
class IProcessPopupView : public IView {
public:
    // No callbacks - state changes are made directly to ViewModels
};

} // namespace pex
