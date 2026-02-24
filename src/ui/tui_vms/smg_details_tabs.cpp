#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <sstream>
#include <iomanip>
#include <cstdio>

namespace pex {

void SmgApp::render_file_handles_tab() {
    if (!details_display_) return;

    const auto& handles = view_model_.details_panel.file_handles;
    const int inner_width = term_cols_ - 4;

    // Header
    char header[128];
    std::snprintf(header, sizeof(header), "%-5s %-10s %s", "FD", "Type", "Path");
    smg_put_chars(details_display_, 3, 2, header, SMG_REND_HEADER);

    if (handles.empty()) {
        smg_put_chars(details_display_, 4, 2, "(no file handles or access denied)", SMG_REND_NORMAL);
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < handles.size() && row < visible_details_rows_ + 4;
         ++i, ++row) {

        const auto& fh = handles[i];
        std::string path = fh.path;
        const int path_width = inner_width - 18;
        if (path_width > 3 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        char line[512];
        std::snprintf(line, sizeof(line), "%-5d %-10s %s",
                  fh.fd, fh.type.c_str(), path.c_str());
        smg_put_chars(details_display_, row, 2, line, SMG_REND_NORMAL);
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        smg_put_chars(details_display_, 3, inner_width - 2, "^^^", SMG_REND_TITLE);
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(handles.size())) {
        smg_put_chars(details_display_, visible_details_rows_ + 3, inner_width - 2, "vvv", SMG_REND_TITLE);
    }
}

void SmgApp::render_network_tab() {
    if (!details_display_) return;

    const auto& connections = view_model_.details_panel.network_connections;
    const int inner_width = term_cols_ - 4;

    // Header
    char header[256];
    std::snprintf(header, sizeof(header), "%-6s %-20s %-20s %-8s",
              "Proto", "Local Address", "Remote Address", "State");
    smg_put_chars(details_display_, 3, 2, header, SMG_REND_HEADER);

    if (connections.empty()) {
        smg_put_chars(details_display_, 4, 2, "(no network connections or access denied)", SMG_REND_NORMAL);
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < connections.size() && row < visible_details_rows_ + 4;
         ++i, ++row) {

        const auto& conn = connections[i];

        auto truncate = [](const std::string& s, int w) -> std::string {
            if (w <= 0) return "";
            if (static_cast<int>(s.size()) <= w) return s;
            if (w <= 3) return s.substr(0, w);
            return s.substr(0, w - 3) + "...";
        };

        char line[512];
        std::snprintf(line, sizeof(line), "%-6s %-20s %-20s %-8s",
                  truncate(conn.protocol, 6).c_str(),
                  truncate(conn.local_endpoint, 20).c_str(),
                  truncate(conn.remote_endpoint, 20).c_str(),
                  truncate(conn.state, 8).c_str());
        smg_put_chars(details_display_, row, 2, line, SMG_REND_NORMAL);
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        smg_put_chars(details_display_, 3, inner_width - 2, "^^^", SMG_REND_TITLE);
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(connections.size())) {
        smg_put_chars(details_display_, visible_details_rows_ + 3, inner_width - 2, "vvv", SMG_REND_TITLE);
    }
}

void SmgApp::render_threads_tab() {
    if (!details_display_) return;

    const auto& threads = view_model_.details_panel.threads;
    const int inner_width = term_cols_ - 4;

    // Header
    char header[256];
    std::snprintf(header, sizeof(header), "%-8s %-20s %-5s %-8s %-4s %s",
              "TID", "Name", "State", "Priority", "CPU", "Function");
    smg_put_chars(details_display_, 3, 2, header, SMG_REND_HEADER);

    if (threads.empty()) {
        smg_put_chars(details_display_, 4, 2, "(no threads or access denied)", SMG_REND_NORMAL);
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < threads.size() && row < visible_details_rows_ + 4;
         ++i, ++row) {

        const auto& thr = threads[i];

        std::string name = thr.name;
        if (name.length() > 20) name = name.substr(0, 17) + "...";

        std::string func = thr.current_library;
        const int func_width = inner_width - 53;
        if (func_width > 0 && static_cast<int>(func.length()) > func_width) {
            func = func.substr(0, func_width - 3) + "...";
        }

        unsigned int state_rend = get_state_rendition(thr.state);

        char line[512];
        std::snprintf(line, sizeof(line), "%-8d %-20s   %c   %-8d %-4d %s",
                  thr.tid,
                  name.c_str(),
                  thr.state,
                  thr.priority,
                  thr.processor,
                  func.c_str());
        smg_put_chars(details_display_, row, 2, line, state_rend);
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        smg_put_chars(details_display_, 3, inner_width - 2, "^^^", SMG_REND_TITLE);
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(threads.size())) {
        smg_put_chars(details_display_, visible_details_rows_ + 3, inner_width - 2, "vvv", SMG_REND_TITLE);
    }
}

void SmgApp::render_memory_tab() {
    if (!details_display_) return;

    const auto& maps = view_model_.details_panel.memory_maps;
    const int inner_width = term_cols_ - 4;

    // Header
    char header[256];
    std::snprintf(header, sizeof(header), "%-18s %-10s %-6s %s", "Address", "Size", "Perms", "Pathname");
    smg_put_chars(details_display_, 3, 2, header, SMG_REND_HEADER);

    if (maps.empty()) {
        smg_put_chars(details_display_, 4, 2, "(no memory maps or access denied)", SMG_REND_NORMAL);
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < maps.size() && row < visible_details_rows_ + 4;
         ++i, ++row) {

        const auto& mm = maps[i];

        std::string path = mm.pathname;
        const int path_width = inner_width - 38;
        if (path_width > 0 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        char line[512];
        std::snprintf(line, sizeof(line), "%-18s %-10s %-6s %s",
                  mm.address.c_str(),
                  mm.size.c_str(),
                  mm.permissions.c_str(),
                  path.c_str());
        smg_put_chars(details_display_, row, 2, line, SMG_REND_NORMAL);
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        smg_put_chars(details_display_, 3, inner_width - 2, "^^^", SMG_REND_TITLE);
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(maps.size())) {
        smg_put_chars(details_display_, visible_details_rows_ + 3, inner_width - 2, "vvv", SMG_REND_TITLE);
    }
}

void SmgApp::render_environment_tab() {
    if (!details_display_) return;

    const auto& vars = view_model_.details_panel.environment_vars;
    const int inner_width = term_cols_ - 4;

    // Header
    char header[128];
    std::snprintf(header, sizeof(header), "%-30s %s", "Variable", "Value");
    smg_put_chars(details_display_, 3, 2, header, SMG_REND_HEADER);

    if (vars.empty()) {
        smg_put_chars(details_display_, 4, 2, "(no environment variables or access denied)", SMG_REND_NORMAL);
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < vars.size() && row < visible_details_rows_ + 4;
         ++i, ++row) {

        const auto& ev = vars[i];

        std::string name = ev.name;
        if (name.length() > 30) name = name.substr(0, 27) + "...";

        std::string value = ev.value;
        const int value_width = inner_width - 33;
        if (value_width > 0 && static_cast<int>(value.length()) > value_width) {
            value = value.substr(0, value_width - 3) + "...";
        }

        char line[512];
        std::snprintf(line, sizeof(line), "%-30s %s", name.c_str(), value.c_str());
        smg_put_chars(details_display_, row, 2, line, SMG_REND_NORMAL);
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        smg_put_chars(details_display_, 3, inner_width - 2, "^^^", SMG_REND_TITLE);
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(vars.size())) {
        smg_put_chars(details_display_, visible_details_rows_ + 3, inner_width - 2, "vvv", SMG_REND_TITLE);
    }
}

void SmgApp::render_libraries_tab() {
    if (!details_display_) return;

    const auto& libs = view_model_.details_panel.libraries;
    const int inner_width = term_cols_ - 4;

    // Header
    char header[256];
    std::snprintf(header, sizeof(header), "%-18s %-10s %-10s %s", "Base Address", "Size", "Resident", "Path");
    smg_put_chars(details_display_, 3, 2, header, SMG_REND_HEADER);

    if (libs.empty()) {
        smg_put_chars(details_display_, 4, 2, "(no libraries or access denied)", SMG_REND_NORMAL);
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < libs.size() && row < visible_details_rows_ + 4;
         ++i, ++row) {

        const auto& lib = libs[i];

        std::string path = lib.path;
        const int path_width = inner_width - 43;
        if (path_width > 0 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        // base_address is already a hex string like "7f1234560000"
        std::string addr = lib.base_address.empty() ? "N/A" : ("0x" + lib.base_address);

        char line[512];
        std::snprintf(line, sizeof(line), "%-18s %-10s %-10s %s",
                  addr.c_str(),
                  format_bytes(lib.total_size).c_str(),
                  format_bytes(lib.resident_size).c_str(),
                  path.c_str());
        smg_put_chars(details_display_, row, 2, line, SMG_REND_NORMAL);
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        smg_put_chars(details_display_, 3, inner_width - 2, "^^^", SMG_REND_TITLE);
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(libs.size())) {
        smg_put_chars(details_display_, visible_details_rows_ + 3, inner_width - 2, "vvv", SMG_REND_TITLE);
    }
}

} // namespace pex
