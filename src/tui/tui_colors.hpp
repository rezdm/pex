#pragma once

#include <ncurses.h>

namespace pex {

// Color pair indices
enum ColorPair {
    COLOR_PAIR_DEFAULT = 1,
    COLOR_PAIR_TITLE,
    COLOR_PAIR_SELECTED,
    COLOR_PAIR_HEADER,
    COLOR_PAIR_BORDER,
    COLOR_PAIR_CPU_BAR,
    COLOR_PAIR_CPU_BAR_USER,
    COLOR_PAIR_CPU_BAR_SYSTEM,
    COLOR_PAIR_MEM_BAR,
    COLOR_PAIR_SWAP_BAR,
    COLOR_PAIR_STATUS,
    COLOR_PAIR_ERROR,
    COLOR_PAIR_WARNING,
    COLOR_PAIR_PROCESS_RUNNING,
    COLOR_PAIR_PROCESS_SLEEPING,
    COLOR_PAIR_PROCESS_ZOMBIE,
    COLOR_PAIR_PROCESS_STOPPED,
    COLOR_PAIR_TAB_ACTIVE,
    COLOR_PAIR_TAB_INACTIVE,
    COLOR_PAIR_SEARCH,
    COLOR_PAIR_HIGHLIGHT,
    COLOR_PAIR_DIALOG,
    COLOR_PAIR_DIALOG_BUTTON,
    COLOR_PAIR_HELP_KEY,
    COLOR_PAIR_TREE_LINE,
};

// Initialize ncurses color pairs
void init_colors();

// Get color pair for process state
int get_state_color(char state);

} // namespace pex
