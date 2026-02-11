#include "smg_colors.hpp"

namespace pex {

unsigned int get_state_rendition(char state) {
    switch (state) {
        case 'R':
            return SMG_REND_PROC_RUNNING;
        case 'S':
        case 'I':
            return SMG_REND_PROC_SLEEPING;
        case 'Z':
            return SMG_REND_PROC_ZOMBIE;
        case 'T':
        case 't':
            return SMG_REND_PROC_STOPPED;
        case 'D':
            return SMG_REND_WARNING;
        default:
            return SMG_REND_NORMAL;
    }
}

} // namespace pex
