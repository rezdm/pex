#include "smg_app.hpp"
#include <algorithm>

#ifdef __VMS
#define __NEW_STARLET 1

// System service for terminal size query via IO$_SENSEMODE
#include <starlet.h>
#include <iodef.h>
#include <iosbdef.h>
#endif

namespace pex {

int SmgApp::calc_system_panel_height() const {
    if (!view_model_.system_panel.is_visible) return 0;
    if (!system_panel_expanded_) return kSystemPanelCollapsedHeight;

    // Expanded: calculate rows needed for all CPUs
    size_t num_cpus = view_model_.system_panel.per_cpu_usage.size();
    if (num_cpus == 0) num_cpus = 1;

    constexpr int bar_width = 15;
    constexpr int cpu_section_width = bar_width + 12;
    const int cpus_per_row = std::max(1, (term_cols_ - 2) / cpu_section_width);

    const int cpu_rows = static_cast<int>((num_cpus + cpus_per_row - 1) / cpus_per_row);
    return cpu_rows + 2;  // +2 for memory row and tasks row
}

void SmgApp::create_displays() {
    terminal_too_small_ = false;

    // Clear all regions
    for (unsigned int i = 0; i < DISP_COUNT; i++) {
        regions_[i] = DisplayRegion{};
    }
    system_display_ = 0;
    process_display_ = 0;
    details_display_ = 0;
    status_display_ = 0;
    dialog_display_ = 0;

    // Minimum terminal size check
    constexpr int kMinWidth = 40;
    constexpr int kMinProcessHeight = 5;
    const int min_system_height = view_model_.system_panel.is_visible
                                  ? kSystemPanelCollapsedHeight : 0;
    const int min_total_height = kStatusBarHeight + kMinDetailsHeight
                                 + kMinProcessHeight + min_system_height;

    if (term_cols_ < kMinWidth || term_rows_ < min_total_height) {
        terminal_too_small_ = true;
        return;
    }

    // Calculate panel heights (same logic as before)
    int system_height = view_model_.system_panel.is_visible
                        ? calc_system_panel_height() : 0;
    const int max_system_height = term_rows_
        - (kStatusBarHeight + kMinDetailsHeight + kMinProcessHeight);
    if (view_model_.system_panel.is_visible && system_height > max_system_height) {
        system_panel_expanded_ = false;
        system_height = kSystemPanelCollapsedHeight;
    }

    constexpr int status_height = kStatusBarHeight;
    const int remaining = std::max(10, term_rows_ - system_height - status_height);
    int process_height = std::max(kMinProcessHeight,
                                  static_cast<int>(remaining * kProcessPanelRatio));
    int details_height = std::max(kMinDetailsHeight, remaining - process_height);
    if (process_height + details_height > remaining) {
        process_height = std::max(kMinProcessHeight, remaining - kMinDetailsHeight);
        details_height = remaining - process_height;
    }

    int y = 1;  // 1-based screen row

    // System panel (no border)
    if (view_model_.system_panel.is_visible && system_height > 0) {
        auto& rgn = regions_[DISP_SYSTEM];
        rgn.row = y;
        rgn.col = 1;
        rgn.inner_rows = system_height;
        rgn.inner_cols = term_cols_;
        rgn.has_border = false;
        rgn.active = true;
        system_display_ = DISP_SYSTEM;
        y += system_height;
    }

    // Process list (with border)
    {
        int inner_rows = process_height - 2;  // -2 for top+bottom border
        int inner_cols = term_cols_ - 2;       // -2 for left+right border
        if (inner_rows < 1) inner_rows = 1;
        if (inner_cols < 1) inner_cols = 1;

        auto& rgn = regions_[DISP_PROCESS];
        rgn.row = y + 1;        // Inner area starts after top border row
        rgn.col = 2;            // Inner area starts after left border col
        rgn.inner_rows = inner_rows;
        rgn.inner_cols = inner_cols;
        rgn.has_border = true;
        rgn.active = true;
        process_display_ = DISP_PROCESS;
        visible_process_rows_ = std::max(1, process_height - 3);  // -2 border -1 header
        y += process_height;
    }

    // Details panel (with border)
    {
        int inner_rows = details_height - 2;
        int inner_cols = term_cols_ - 2;
        if (inner_rows < 1) inner_rows = 1;
        if (inner_cols < 1) inner_cols = 1;

        auto& rgn = regions_[DISP_DETAILS];
        rgn.row = y + 1;
        rgn.col = 2;
        rgn.inner_rows = inner_rows;
        rgn.inner_cols = inner_cols;
        rgn.has_border = true;
        rgn.active = true;
        details_display_ = DISP_DETAILS;
        visible_details_rows_ = std::max(1, details_height - 4);  // -2 border -1 tabs -1 sep
        y += details_height;
    }

    // Status bar (no border, reverse video via rendition)
    {
        auto& rgn = regions_[DISP_STATUS];
        rgn.row = y;
        rgn.col = 1;
        rgn.inner_rows = status_height;
        rgn.inner_cols = term_cols_;
        rgn.has_border = false;
        rgn.active = true;
        status_display_ = DISP_STATUS;
    }
}

void SmgApp::resize_displays() {
    cleanup_displays();

#ifdef __VMS
    // Re-query terminal dimensions via IO$_SENSEMODE on our TT: channel.
    // Avoids SMG$ pasteboard which interferes with direct ANSI/QIO rendering.
    if (tt_chan_ != 0) {
        unsigned char tt_chars[12] = {0};
        _iosb iosb = {};
        unsigned int status = sys$qiow(0, tt_chan_, IO$_SENSEMODE, &iosb, 0, 0,
                                        tt_chars, 12, 0, 0, 0, 0);
        if ((status & 1) && (iosb.iosb$w_status & 1)) {
            unsigned short width  = static_cast<unsigned short>(tt_chars[2] | (tt_chars[3] << 8));
            unsigned short length = static_cast<unsigned short>(tt_chars[8] | (tt_chars[9] << 8));
            if (width > 0)  term_cols_ = width;
            if (length > 0) term_rows_ = length;
        }
    }
#endif

    create_displays();
    init_cell_buffers();  // resize buffers and force full redraw
}

void SmgApp::cleanup_displays() {
    // Deactivate all display regions
    for (unsigned int i = 0; i < DISP_COUNT; i++) {
        regions_[i] = DisplayRegion{};
    }
    system_display_ = 0;
    process_display_ = 0;
    details_display_ = 0;
    status_display_ = 0;
    dialog_display_ = 0;
}

} // namespace pex
