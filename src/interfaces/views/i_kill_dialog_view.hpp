#pragma once

#include "../i_view.hpp"

namespace pex {

// Simple view interface for kill confirmation dialog
class IKillDialogView : public IView {
public:
    // No callbacks - state changes are made directly to ViewModels
};

} // namespace pex
