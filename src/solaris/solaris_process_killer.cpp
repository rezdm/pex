#include "solaris_process_killer.hpp"
#include <stdexcept>

namespace pex {

[[noreturn]] static void throw_not_implemented(const char* func) {
    throw std::runtime_error(std::string("Solaris: ") + func + " not implemented");
}

KillResult SolarisProcessKiller::kill_process(int /*pid*/, bool /*force*/) {
    throw_not_implemented(__func__);
}

KillResult SolarisProcessKiller::kill_process_tree(int /*pid*/, bool /*force*/) {
    throw_not_implemented(__func__);
}

} // namespace pex
