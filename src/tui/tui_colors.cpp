#include "tui_colors.hpp"

namespace pex {

void init_colors() {
    if (!has_colors()) {
        return;
    }

    start_color();
    use_default_colors();

    // Basic colors
    init_pair(COLOR_PAIR_DEFAULT, -1, -1);  // Default terminal colors
    init_pair(COLOR_PAIR_TITLE, COLOR_CYAN, -1);
    init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_PAIR_HEADER, COLOR_YELLOW, -1);
    init_pair(COLOR_PAIR_BORDER, COLOR_BLUE, -1);

    // Progress bars
    init_pair(COLOR_PAIR_CPU_BAR, COLOR_GREEN, -1);
    init_pair(COLOR_PAIR_CPU_BAR_USER, COLOR_GREEN, -1);
    init_pair(COLOR_PAIR_CPU_BAR_SYSTEM, COLOR_RED, -1);
    init_pair(COLOR_PAIR_MEM_BAR, COLOR_CYAN, -1);
    init_pair(COLOR_PAIR_SWAP_BAR, COLOR_YELLOW, -1);

    // Status
    init_pair(COLOR_PAIR_STATUS, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_PAIR_ERROR, COLOR_RED, -1);
    init_pair(COLOR_PAIR_WARNING, COLOR_YELLOW, -1);

    // Process states
    init_pair(COLOR_PAIR_PROCESS_RUNNING, COLOR_GREEN, -1);
    init_pair(COLOR_PAIR_PROCESS_SLEEPING, -1, -1);
    init_pair(COLOR_PAIR_PROCESS_ZOMBIE, COLOR_RED, -1);
    init_pair(COLOR_PAIR_PROCESS_STOPPED, COLOR_YELLOW, -1);

    // Tabs
    init_pair(COLOR_PAIR_TAB_ACTIVE, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_PAIR_TAB_INACTIVE, COLOR_WHITE, -1);

    // Search and highlight
    init_pair(COLOR_PAIR_SEARCH, COLOR_BLACK, COLOR_YELLOW);
    init_pair(COLOR_PAIR_HIGHLIGHT, COLOR_BLACK, COLOR_GREEN);

    // Dialog
    init_pair(COLOR_PAIR_DIALOG, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_PAIR_DIALOG_BUTTON, COLOR_BLACK, COLOR_WHITE);

    // Help
    init_pair(COLOR_PAIR_HELP_KEY, COLOR_CYAN, -1);

    // Tree lines
    init_pair(COLOR_PAIR_TREE_LINE, COLOR_BLUE, -1);
}

int get_state_color(char state) {
    switch (state) {
        case 'R':
            return COLOR_PAIR_PROCESS_RUNNING;
        case 'S':
        case 'I':
            return COLOR_PAIR_PROCESS_SLEEPING;
        case 'Z':
            return COLOR_PAIR_PROCESS_ZOMBIE;
        case 'T':
        case 't':
            return COLOR_PAIR_PROCESS_STOPPED;
        case 'D':
            return COLOR_PAIR_WARNING;
        default:
            return COLOR_PAIR_DEFAULT;
    }
}

} // namespace pex
