#pragma once

#include "../process_info.hpp"
#include <vector>
#include <string>

namespace pex {

// Tab identifiers for the details panel
enum class DetailsTab {
    FileHandles,
    Network,
    Threads,
    Memory,
    Environment,
    Libraries
};

struct TabSortState {
    int column = 0;
    bool ascending = true;
};

struct DetailsPanelViewModel {
    // Which process details are being shown
    int details_pid = -1;

    // Active tab
    DetailsTab active_tab = DetailsTab::FileHandles;

    // Tab data (fetched on-demand)
    std::vector<FileHandleInfo> file_handles;
    std::vector<NetworkConnectionInfo> network_connections;
    std::vector<ThreadInfo> threads;
    std::vector<MemoryMapInfo> memory_maps;
    std::vector<EnvironmentVariable> environment_vars;
    std::vector<LibraryInfo> libraries;

    // Sorting state for each tab
    TabSortState file_handles_sort;
    TabSortState network_sort;
    TabSortState threads_sort;
    TabSortState memory_sort;
    TabSortState environment_sort;
    TabSortState libraries_sort;

    // Thread selection for stack view
    int selected_thread_idx = -1;
    int selected_thread_tid = -1;
    int cached_stack_tid = -1;
    std::string cached_stack;

    // Dirty flag for re-sorting after refresh
    bool details_dirty = false;
};

} // namespace pex
