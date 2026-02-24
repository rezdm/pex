#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <sstream>

namespace pex {

void SmgApp::render_kill_dialog() {
    const auto& kd = view_model_.kill_dialog;
    if (!kd.is_visible) return;

    // Calculate dialog dimensions
    int dialog_inner_width = 48;
    int dialog_inner_height = kd.show_force_option ? 8 : 6;
    if (!kd.error_message.empty()) dialog_inner_height += 2;

    // Set up the DISP_DIALOG region, centered on screen
    int total_height = dialog_inner_height + 2;  // +2 for border
    int total_width = dialog_inner_width + 2;

    int paste_row = (term_rows_ - total_height) / 2 + 1;
    int paste_col = (term_cols_ - total_width) / 2 + 1;
    if (paste_row < 1) paste_row = 1;
    if (paste_col < 1) paste_col = 1;

    auto& rgn = regions_[DISP_DIALOG];
    rgn.row = paste_row + 1;       // Inner area starts after top border
    rgn.col = paste_col + 1;       // Inner area starts after left border
    rgn.inner_rows = dialog_inner_height;
    rgn.inner_cols = dialog_inner_width;
    rgn.has_border = true;
    rgn.active = true;
    dialog_display_ = DISP_DIALOG;

    // Draw border and clear interior
    draw_border(rgn);
    smg_erase_display(DISP_DIALOG);

    // Title on border line
    smg_draw_box_title(DISP_DIALOG,
                       kd.is_tree_kill ? "Kill Process Tree" : "Kill Process");

    // Process info
    std::string msg;
    if (kd.is_tree_kill) {
        msg = "Kill process tree starting at:";
    } else {
        msg = "Kill process:";
    }
    smg_put_chars(DISP_DIALOG, 2, 2, msg, SMG_REND_DIALOG);

    std::ostringstream proc_info;
    proc_info << kd.target_name << " (PID " << kd.target_pid << ")";
    smg_put_chars(DISP_DIALOG, 3, 4, proc_info.str(), SMG_REND_DIALOG | SMG_REND_BOLD);

    int row = 5;

    // Error message
    if (!kd.error_message.empty()) {
        smg_put_chars(DISP_DIALOG, row, 2, kd.error_message, SMG_REND_ERROR);
        row += 2;
    }

    // Buttons — VMS uses $FORCEX/$DELPRC instead of SIGTERM/SIGKILL
    if (kd.show_force_option) {
        smg_put_chars(DISP_DIALOG, row, 2,
                      "Process did not terminate. Force kill?", SMG_REND_DIALOG);
        row++;
        smg_put_chars(DISP_DIALOG, row, 6,
                      " [Y] Force Kill ($DELPRC) ", SMG_REND_DIALOG_BTN);
        smg_put_chars(DISP_DIALOG, row, 35, " [N] Cancel ", SMG_REND_DIALOG);
    } else {
        smg_put_chars(DISP_DIALOG, row, 6,
                      " [Y] Kill ($FORCEX) ", SMG_REND_DIALOG_BTN);
        smg_put_chars(DISP_DIALOG, row, 30, " [N] Cancel ", SMG_REND_DIALOG);
    }
}

void SmgApp::render_help_overlay() {
    int help_inner_width = 58;
    int help_inner_height = 29;

    // Set up the DISP_DIALOG region, centered on screen
    int total_height = help_inner_height + 2;
    int total_width = help_inner_width + 2;

    int paste_row = (term_rows_ - total_height) / 2 + 1;
    int paste_col = (term_cols_ - total_width) / 2 + 1;
    if (paste_row < 1) paste_row = 1;
    if (paste_col < 1) paste_col = 1;

    auto& rgn = regions_[DISP_DIALOG];
    rgn.row = paste_row + 1;
    rgn.col = paste_col + 1;
    rgn.inner_rows = help_inner_height;
    rgn.inner_cols = help_inner_width;
    rgn.has_border = true;
    rgn.active = true;
    dialog_display_ = DISP_DIALOG;

    // Draw border and clear interior
    draw_border(rgn);
    smg_erase_display(DISP_DIALOG);

    // Title on border line
    smg_draw_box_title(DISP_DIALOG, "Help");

    // Help content
    struct HelpLine {
        const char* text;
        bool is_heading;
    };

    const HelpLine help_lines[] = {
        {"Navigation:",                                    true},
        {"  Up/k, Down/j    Move selection up/down",       false},
        {"  PgUp, PgDn      Page up/down",                 false},
        {"  g, G            Jump to first/last",           false},
        {"  Tab             Switch panel focus",           false},
        {"",                                               false},
        {"Process Tree:",                                  true},
        {"  Enter/Right     Expand node",                  false},
        {"  Left            Collapse node or go to parent",false},
        {"  </> or ,/.      Horizontal scroll",            false},
        {"  0               Reset horizontal scroll",      false},
        {"  s               Toggle system panel",          false},
        {"  c               Expand/collapse CPUs",         false},
        {"",                                               false},
        {"Details Panel:",                                 true},
        {"  1-6             Switch tab by number",         false},
        {"  f/w/h/m/e/l     Switch tab by letter",         false},
        {"  Left/Right      Previous/next tab",            false},
        {"",                                               false},
        {"Actions:",                                       true},
        {"  /               Search mode",                  false},
        {"  n/N             Next/previous search match",   false},
        {"  x               Kill process ($FORCEX)",       false},
        {"  K               Kill process tree",            false},
        {"  r               Force refresh",                false},
        {"  Ctrl+Z / q      Quit",                         false},
        {"  ? / PF1         This help",                    false},
    };

    int row = 2;
    for (const auto& line : help_lines) {
        if (line.text[0] == '\0') {
            row++;
            continue;
        }

        if (line.is_heading) {
            smg_put_chars(DISP_DIALOG, row, 2, line.text, SMG_REND_BOLD);
        } else {
            // Key part (first 18 chars) in highlight
            std::string key_part(line.text, 2, 16);
            std::string desc_part(line.text + 18);
            smg_put_chars(DISP_DIALOG, row, 2, key_part, SMG_REND_HELP_KEY);
            smg_put_chars(DISP_DIALOG, row, 18, desc_part, SMG_REND_NORMAL);
        }
        row++;
    }

    // Close instruction
    smg_put_chars(DISP_DIALOG, help_inner_height, 17,
                  " Press any key to close ", SMG_REND_DIALOG_BTN);
}

void SmgApp::render_status_bar() {
    if (!status_display_) return;

    smg_erase_display(status_display_);

    // Status bar uses reverse video
    const char* hints = "q:Quit  /:Search  s:System  c:CPUs  Tab:Panel  x:Kill  ?:Help";
    smg_put_chars(status_display_, 1, 1, hints, SMG_REND_STATUS);

    // Right side: search text if active
    if (!view_model_.process_list.search_text.empty()) {
        std::string search_indicator = "Search: " + view_model_.process_list.search_text;
        if (search_indicator.length() > 30) {
            search_indicator = search_indicator.substr(0, 27) + "...";
        }
        int search_x = term_cols_ - static_cast<int>(search_indicator.length()) - 2;
        if (search_x > 0) {
            smg_put_chars(status_display_, 1, search_x, search_indicator, SMG_REND_STATUS);
        }
    }
}

void SmgApp::render_search_bar() {
    int search_inner_width = 48;
    int search_inner_height = 1;

    // Set up the DISP_DIALOG region near the bottom of the screen
    int total_height = search_inner_height + 2;
    int total_width = search_inner_width + 2;

    int paste_row = term_rows_ - 3;
    int paste_col = (term_cols_ - total_width) / 2 + 1;
    if (paste_row < 1) paste_row = 1;
    if (paste_col < 1) paste_col = 1;

    auto& rgn = regions_[DISP_DIALOG];
    rgn.row = paste_row + 1;
    rgn.col = paste_col + 1;
    rgn.inner_rows = search_inner_height;
    rgn.inner_cols = search_inner_width;
    rgn.has_border = true;
    rgn.active = true;
    dialog_display_ = DISP_DIALOG;

    // Draw border and clear interior
    draw_border(rgn);
    smg_erase_display(DISP_DIALOG);

    // Title on border line
    smg_draw_box_title(DISP_DIALOG, "Search");

    // Search input
    std::string prompt = "/ " + search_input_;
    smg_put_chars(DISP_DIALOG, 1, 2, prompt, SMG_REND_NORMAL);
}

} // namespace pex
