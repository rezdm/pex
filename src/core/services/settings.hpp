#pragma once

#include <map>
#include <string>

namespace pex {

// Minimal key=value settings persistence (issue #1).
// File: $XDG_CONFIG_HOME/pex/pex.conf (or ~/.config/pex/pex.conf).
// The whole file is loaded into a map and written back on save, so unknown
// keys are preserved - the GUI and TUI can share one file without clobbering
// each other's settings. (Per-window/table layout of the GUI is persisted
// separately by Dear ImGui in imgui.ini.)
class Settings {
public:
    Settings();  // Resolves the path; call load() to read the file

    void load();
    bool save() const;  // Creates the config directory if needed

    [[nodiscard]] int get_int(const std::string& key, int def) const;
    [[nodiscard]] bool get_bool(const std::string& key, bool def) const;
    void set_int(const std::string& key, int value);
    void set_bool(const std::string& key, bool value);

    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
    std::map<std::string, std::string> values_;
};

} // namespace pex
