#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <charconv>
#include <algorithm>

namespace pex {

void TuiApp::render_file_handles_tab() const {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& handles = view_model_.details_panel.file_handles;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-5s %-10s %s", "FD", "Type", "Path");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (handles.empty()) {
        mvwprintw(details_win_, 4, 2, "(no file handles or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < handles.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& fh = handles[i];
        std::string path = fh.path;
        const int path_width = max_x - 20;
        if (static_cast<int>(path.length()) > path_width && path_width > 3) {
            path = path.substr(0, path_width - 3) + "...";
        }

        mvwprintw(details_win_, row, 2, "%-5d %-10s %s",
                  fh.fd, fh.type.c_str(), path.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(handles.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_network_tab() const {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& connections = view_model_.details_panel.network_connections;

    auto normalize_ip = [](std::string ip) -> std::string {
        if (ip.size() >= 2 && ip.front() == '[' && ip.back() == ']') {
            ip = ip.substr(1, ip.size() - 2);
        }
        return ip;
    };

    auto parse_endpoint = [&normalize_ip](const std::string& endpoint) -> std::pair<std::string, uint16_t> {
        const size_t colon_pos = endpoint.rfind(':');
        if (colon_pos == std::string::npos) return {endpoint, 0};

        std::string ip = normalize_ip(endpoint.substr(0, colon_pos));
        uint16_t port = 0;
        std::from_chars(endpoint.data() + colon_pos + 1,
                       endpoint.data() + endpoint.size(), port);
        return {ip, port};
    };

    auto format_numeric_endpoint = [&](const std::string& endpoint) -> std::string {
        auto [ip, port] = parse_endpoint(endpoint);
        if (ip.empty()) return endpoint;

        std::string label = ip;
        if (port > 0) {
            if (label.find(':') != std::string::npos && label.front() != '[') {
                label = "[" + label + "]";
            }
            label += ":" + std::to_string(port);
        }
        return label;
    };

    auto format_remote_name = [&](const std::string& endpoint) -> std::string {
        auto [ip, _port] = parse_endpoint(endpoint);
        if (ip.empty()) return {};
        const std::string host = name_resolver_.get_hostname(ip);
        if (host.empty()) return "-";
        return host;
    };

    auto fit = [](const std::string& text, const int width) -> std::string {
        if (width <= 0) return {};
        if (static_cast<int>(text.size()) <= width) return text;
        if (width <= 3) return text.substr(0, width);
        return text.substr(0, width - 3) + "...";
    };

    const int proto_w = 6;
    const int state_w = 8;
    const int gap = 1;
    const int usable = max_x - 2 - 2 - proto_w - state_w - gap * 4;
    const int min_addr_w = 8;
    const int min_name_w = 4;

    int addr_w = (usable > 0) ? std::max(min_addr_w, usable / 3) : min_addr_w;
    int name_w = usable - addr_w * 2;
    if (name_w < min_name_w) {
        name_w = min_name_w;
        addr_w = (usable - name_w) / 2;
        if (addr_w < min_addr_w) addr_w = min_addr_w;
        name_w = std::max(0, usable - addr_w * 2);
    }

    const int proto_x = 2;
    const int local_x = proto_x + proto_w + gap;
    const int remote_x = local_x + addr_w + gap;
    const int remote_name_x = remote_x + addr_w + gap;
    const int state_x = remote_name_x + name_w + gap;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, proto_x, "%-*s", proto_w, fit("Proto", proto_w).c_str());
    mvwprintw(details_win_, 3, local_x, "%-*s", addr_w, fit("Local Address", addr_w).c_str());
    mvwprintw(details_win_, 3, remote_x, "%-*s", addr_w, fit("Remote Address", addr_w).c_str());
    mvwprintw(details_win_, 3, remote_name_x, "%-*s", name_w, fit("Remote Name", name_w).c_str());
    mvwprintw(details_win_, 3, state_x, "%-*s", state_w, fit("State", state_w).c_str());
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (connections.empty()) {
        mvwprintw(details_win_, 4, 2, "(no network connections or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < connections.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& conn = connections[i];

        std::string local = fit(format_numeric_endpoint(conn.local_endpoint), addr_w);
        std::string remote = fit(format_numeric_endpoint(conn.remote_endpoint), addr_w);
        std::string remote_name = fit(format_remote_name(conn.remote_endpoint), name_w);

        mvwprintw(details_win_, row, proto_x, "%-*s", proto_w, fit(conn.protocol, proto_w).c_str());
        mvwprintw(details_win_, row, local_x, "%-*s", addr_w, local.c_str());
        mvwprintw(details_win_, row, remote_x, "%-*s", addr_w, remote.c_str());
        mvwprintw(details_win_, row, remote_name_x, "%-*s", name_w, remote_name.c_str());
        mvwprintw(details_win_, row, state_x, "%-*s", state_w, fit(conn.state, state_w).c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(connections.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_threads_tab() const {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& threads = view_model_.details_panel.threads;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-8s %-20s %-5s %-8s %-4s %s",
              "TID", "Name", "State", "Priority", "CPU", "Function");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (threads.empty()) {
        mvwprintw(details_win_, 4, 2, "(no threads or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < threads.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& thr = threads[i];

        std::string name = thr.name;
        if (name.length() > 20) name = name.substr(0, 17) + "...";

        std::string func = thr.current_library;
        const int func_width = max_x - 55;
        if (func_width > 0 && static_cast<int>(func.length()) > func_width) {
            func = func.substr(0, func_width - 3) + "...";
        }

        const int state_color = get_state_color(thr.state);
        wattron(details_win_, COLOR_PAIR(state_color));

        mvwprintw(details_win_, row, 2, "%-8d %-20s   %c   %-8d %-4d %s",
                  thr.tid,
                  name.c_str(),
                  thr.state,
                  thr.priority,
                  thr.processor,
                  func.c_str());

        wattroff(details_win_, COLOR_PAIR(state_color));
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(threads.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_memory_tab() const {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& maps = view_model_.details_panel.memory_maps;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-18s %-10s %-6s %s", "Address", "Size", "Perms", "Pathname");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (maps.empty()) {
        mvwprintw(details_win_, 4, 2, "(no memory maps or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < maps.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& mm = maps[i];

        std::string path = mm.pathname;
        const int path_width = max_x - 40;
        if (path_width > 0 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        mvwprintw(details_win_, row, 2, "%-18s %-10s %-6s %s",
                  mm.address.c_str(),
                  mm.size.c_str(),
                  mm.permissions.c_str(),
                  path.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(maps.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_environment_tab() const {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& vars = view_model_.details_panel.environment_vars;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-30s %s", "Variable", "Value");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (vars.empty()) {
        mvwprintw(details_win_, 4, 2, "(no environment variables or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < vars.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& ev = vars[i];

        std::string name = ev.name;
        if (name.length() > 30) name = name.substr(0, 27) + "...";

        std::string value = ev.value;
        const int value_width = max_x - 35;
        if (value_width > 0 && static_cast<int>(value.length()) > value_width) {
            value = value.substr(0, value_width - 3) + "...";
        }

        mvwprintw(details_win_, row, 2, "%-30s %s", name.c_str(), value.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(vars.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_libraries_tab() const {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& libs = view_model_.details_panel.libraries;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-18s %-10s %-10s %s", "Base Address", "Size", "Resident", "Path");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (libs.empty()) {
        mvwprintw(details_win_, 4, 2, "(no libraries or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < libs.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& lib = libs[i];

        std::string path = lib.path;
        const int path_width = max_x - 45;
        if (path_width > 0 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        std::ostringstream addr;
        addr << "0x" << std::hex << lib.base_address;

        mvwprintw(details_win_, row, 2, "%-18s %-10s %-10s %s",
                  addr.str().c_str(),
                  format_bytes(lib.total_size).c_str(),
                  format_bytes(lib.resident_size).c_str(),
                  path.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(libs.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

} // namespace pex
