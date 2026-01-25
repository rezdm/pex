#include "freebsd_process_killer.hpp"
#include <stdexcept>

namespace pex {

[[noreturn]] static void throw_not_implemented(const char* func) {
    throw std::runtime_error(std::string("FreeBSD: ") + func + " not implemented");
}

KillResult FreeBSDProcessKiller::kill_process(int /*pid*/, bool /*force*/) {
    throw_not_implemented(__func__);
}

KillResult FreeBSDProcessKiller::kill_process_tree(int /*pid*/, bool /*force*/) {
    throw_not_implemented(__func__);
}

} // namespace pex
