#pragma once

#include "../data_store.hpp"
#include <set>
#include <string>
#include <memory>

namespace pex {

struct ProcessListViewModel {
    // Data snapshot from DataStore
    std::shared_ptr<DataSnapshot> data;

    // Selection state
    int selected_pid = -1;

    // Tree view state
    bool is_tree_view = true;
    std::set<int> collapsed_pids;  // Track which nodes are collapsed

    // Sorting state (for list view)
    int sort_column = 1;  // Default: PID column
    bool sort_ascending = true;

    // Search state
    char search_buffer[256] = {};
    std::string search_text;

    // UI flags
    bool scroll_to_selected = false;
    bool focus_search_box = false;
};

} // namespace pex
