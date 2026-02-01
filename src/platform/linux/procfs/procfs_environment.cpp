#include "../procfs_reader.hpp"
#include <algorithm>

namespace pex {

std::vector<EnvironmentVariable> ProcfsReader::get_environment_variables(const int pid) {
    std::vector<EnvironmentVariable> vars;
    const std::string env_path = "/proc/" + std::to_string(pid) + "/environ";

    std::string content = read_file(env_path);
    if (content.empty()) return vars;

    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\0', start);
        if (end == std::string::npos) end = content.size();

        std::string entry = content.substr(start, end - start);
        if (const size_t eq = entry.find('='); eq != std::string::npos) {
            EnvironmentVariable var;
            var.name = entry.substr(0, eq);
            var.value = entry.substr(eq + 1);
            vars.push_back(std::move(var));
        }

        start = end + 1;
    }

    std::ranges::sort(vars, [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    return vars;
}

} // namespace pex
