#pragma once

#include "../i_view.hpp"

namespace pex {

class ISystemPanelView : public IView {
public:
    // No callbacks needed for system panel - it's display only
};

} // namespace pex
