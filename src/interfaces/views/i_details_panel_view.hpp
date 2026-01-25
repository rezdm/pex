#pragma once

#include "../i_view.hpp"

namespace pex {

// Simple view interface for details panel
// The DetailsTab enum is now defined in details_panel_view_model.hpp
class IDetailsPanelView : public IView {
public:
    // No callbacks - ImGui immediate mode handles interactions inline
};

} // namespace pex
