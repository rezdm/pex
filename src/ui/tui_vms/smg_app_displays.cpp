#include "smg_app.hpp"
#include <algorithm>

#ifdef __VMS
#define __NEW_STARLET 1
#include <smgdef.h>
#include <smg$routines.h>
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
#ifdef __VMS
    unsigned int status;
    unsigned int border_flag = SMG$M_BORDER;
    unsigned int no_border = 0;

    terminal_too_small_ = false;

    // Minimum terminal size check
    constexpr int kMinWidth = 40;
    constexpr int kMinProcessHeight = 5;
    const int min_system_height = view_model_.system_panel.is_visible ? kSystemPanelCollapsedHeight : 0;
    const int min_total_height = kStatusBarHeight + kMinDetailsHeight + kMinProcessHeight + min_system_height;

    if (term_cols_ < kMinWidth || term_rows_ < min_total_height) {
        terminal_too_small_ = true;
        return;
    }

    // Calculate panel heights
    int system_height = view_model_.system_panel.is_visible ? calc_system_panel_height() : 0;
    const int max_system_height = term_rows_ - (kStatusBarHeight + kMinDetailsHeight + kMinProcessHeight);
    if (view_model_.system_panel.is_visible && system_height > max_system_height) {
        system_panel_expanded_ = false;
        system_height = kSystemPanelCollapsedHeight;
    }
    constexpr int status_height = kStatusBarHeight;
    const int remaining = std::max(10, term_rows_ - system_height - status_height);
    int process_height = std::max(kMinProcessHeight, static_cast<int>(remaining * kProcessPanelRatio));
    int details_height = std::max(kMinDetailsHeight, remaining - process_height);
    if (process_height + details_height > remaining) {
        process_height = std::max(kMinProcessHeight, remaining - kMinDetailsHeight);
        details_height = remaining - process_height;
    }

    int y = 1;  // SMG$ uses 1-based coordinates

    // Create system panel display (no border - we render our own labels)
    if (view_model_.system_panel.is_visible && system_height > 0) {
        int rows = system_height;
        int cols = term_cols_;
        status = smg$create_virtual_display(&rows, &cols, &system_display_, &no_border);
        if (status & 1) {
            int paste_row = y;
            int paste_col = 1;
            smg$paste_virtual_display(&system_display_, &pasteboard_id_, &paste_row, &paste_col);
        }
        y += system_height;
    }

    // Create process list display (with border)
    {
        // SMG$M_BORDER adds 2 rows and 2 cols to the virtual display
        int rows = process_height - 2;  // Inner rows (border adds 2)
        int cols = term_cols_ - 2;      // Inner cols (border adds 2)
        if (rows < 1) rows = 1;
        if (cols < 1) cols = 1;
        status = smg$create_virtual_display(&rows, &cols, &process_display_, &border_flag);
        if (status & 1) {
            int paste_row = y;
            int paste_col = 1;
            smg$paste_virtual_display(&process_display_, &pasteboard_id_, &paste_row, &paste_col);
        }
        visible_process_rows_ = std::max(1, process_height - 3);  // -2 border -1 header
        y += process_height;
    }

    // Create details display (with border)
    {
        int rows = details_height - 2;
        int cols = term_cols_ - 2;
        if (rows < 1) rows = 1;
        if (cols < 1) cols = 1;
        status = smg$create_virtual_display(&rows, &cols, &details_display_, &border_flag);
        if (status & 1) {
            int paste_row = y;
            int paste_col = 1;
            smg$paste_virtual_display(&details_display_, &pasteboard_id_, &paste_row, &paste_col);
        }
        visible_details_rows_ = std::max(1, details_height - 4);  // -2 border -1 tabs -1 separator
        y += details_height;
    }

    // Create status bar display (no border, reverse video via rendition)
    {
        int rows = status_height;
        int cols = term_cols_;
        status = smg$create_virtual_display(&rows, &cols, &status_display_, &no_border);
        if (status & 1) {
            int paste_row = y;
            int paste_col = 1;
            smg$paste_virtual_display(&status_display_, &pasteboard_id_, &paste_row, &paste_col);
        }
    }
#endif
}

void SmgApp::resize_displays() {
    cleanup_displays();

#ifdef __VMS
    // Re-query terminal dimensions (SMG$ expects unsigned int*)
    unsigned int rows = 0, cols = 0;
    smg$get_pasteboard_attributes(&pasteboard_id_, &rows, &cols);
    term_rows_ = static_cast<int>(rows);
    term_cols_ = static_cast<int>(cols);
#endif

    create_displays();
}

void SmgApp::cleanup_displays() {
#ifdef __VMS
    if (system_display_) {
        smg$unpaste_virtual_display(&system_display_, &pasteboard_id_);
        smg$delete_virtual_display(&system_display_);
        system_display_ = 0;
    }
    if (process_display_) {
        smg$unpaste_virtual_display(&process_display_, &pasteboard_id_);
        smg$delete_virtual_display(&process_display_);
        process_display_ = 0;
    }
    if (details_display_) {
        smg$unpaste_virtual_display(&details_display_, &pasteboard_id_);
        smg$delete_virtual_display(&details_display_);
        details_display_ = 0;
    }
    if (status_display_) {
        smg$unpaste_virtual_display(&status_display_, &pasteboard_id_);
        smg$delete_virtual_display(&status_display_);
        status_display_ = 0;
    }
    if (dialog_display_) {
        smg$unpaste_virtual_display(&dialog_display_, &pasteboard_id_);
        smg$delete_virtual_display(&dialog_display_);
        dialog_display_ = 0;
    }
#endif
}

} // namespace pex
