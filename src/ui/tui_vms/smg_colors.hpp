#pragma once

// ANSI rendition mapping for OpenVMS TUI
//
// Since SMG$ display output doesn't work on VMS x86_64 SSH pseudo-terminals,
// we bypass SMG$ for rendering and use direct ANSI escape sequences via
// SYS$QIOW. Renditions are encoded as a bitmask that gets converted to
// ANSI SGR (Select Graphic Rendition) sequences at render time.
//
// Bitmask layout:
//   Bits 0-3:  Attributes (bold, reverse, underline, blink)
//   Bits 4-7:  Foreground color index (0=default, 1=red, 2=green, 3=yellow,
//              4=blue, 5=magenta, 6=cyan, 7=white)
//   Bits 8-11: Background color index (same mapping, 0=default)

namespace pex {

// Attribute bits
constexpr unsigned int ANSI_BOLD      = 0x0001;
constexpr unsigned int ANSI_REVERSE   = 0x0002;
constexpr unsigned int ANSI_UNDERLINE = 0x0004;
constexpr unsigned int ANSI_BLINK     = 0x0008;

// Foreground color encoding
constexpr unsigned int ANSI_FG_SHIFT  = 4;
constexpr unsigned int ANSI_FG_MASK   = 0x00F0;
constexpr unsigned int ANSI_FG_RED    = (1u << ANSI_FG_SHIFT);  // 0x0010
constexpr unsigned int ANSI_FG_GREEN  = (2u << ANSI_FG_SHIFT);  // 0x0020
constexpr unsigned int ANSI_FG_YELLOW = (3u << ANSI_FG_SHIFT);  // 0x0030
constexpr unsigned int ANSI_FG_BLUE   = (4u << ANSI_FG_SHIFT);  // 0x0040
constexpr unsigned int ANSI_FG_MAGENTA= (5u << ANSI_FG_SHIFT);  // 0x0050
constexpr unsigned int ANSI_FG_CYAN   = (6u << ANSI_FG_SHIFT);  // 0x0060
constexpr unsigned int ANSI_FG_WHITE  = (7u << ANSI_FG_SHIFT);  // 0x0070

// Background color encoding
constexpr unsigned int ANSI_BG_SHIFT  = 8;
constexpr unsigned int ANSI_BG_MASK   = 0x0F00;
constexpr unsigned int ANSI_BG_RED    = (1u << ANSI_BG_SHIFT);
constexpr unsigned int ANSI_BG_GREEN  = (2u << ANSI_BG_SHIFT);
constexpr unsigned int ANSI_BG_YELLOW = (3u << ANSI_BG_SHIFT);
constexpr unsigned int ANSI_BG_BLUE   = (4u << ANSI_BG_SHIFT);
constexpr unsigned int ANSI_BG_CYAN   = (6u << ANSI_BG_SHIFT);

// ---- Named rendition constants (same names as before for API compatibility) ----

constexpr unsigned int SMG_REND_NORMAL    = 0;
constexpr unsigned int SMG_REND_BOLD      = ANSI_BOLD;
constexpr unsigned int SMG_REND_REVERSE   = ANSI_REVERSE;
constexpr unsigned int SMG_REND_UNDERLINE = ANSI_UNDERLINE;
constexpr unsigned int SMG_REND_BLINK     = ANSI_BLINK;

// Color-mapped renditions (replacing SMG$ USER1-USER8)
constexpr unsigned int SMG_REND_GREEN     = ANSI_FG_GREEN;
constexpr unsigned int SMG_REND_RED       = ANSI_FG_RED;
constexpr unsigned int SMG_REND_CYAN      = ANSI_FG_CYAN;
constexpr unsigned int SMG_REND_YELLOW    = ANSI_FG_YELLOW;
constexpr unsigned int SMG_REND_BLUE      = ANSI_FG_BLUE;
constexpr unsigned int SMG_REND_DIALOG    = ANSI_FG_WHITE | ANSI_BG_BLUE;
constexpr unsigned int SMG_REND_SELECTED  = ANSI_FG_CYAN | ANSI_REVERSE;
constexpr unsigned int SMG_REND_SEARCH    = ANSI_BG_YELLOW | ANSI_BOLD;

// Compound renditions
constexpr unsigned int SMG_REND_TITLE      = ANSI_FG_CYAN | ANSI_BOLD;
constexpr unsigned int SMG_REND_HEADER     = ANSI_FG_YELLOW | ANSI_BOLD;
constexpr unsigned int SMG_REND_STATUS     = ANSI_FG_BLUE | ANSI_REVERSE;
constexpr unsigned int SMG_REND_ERROR      = ANSI_FG_RED | ANSI_BOLD;
constexpr unsigned int SMG_REND_WARNING    = ANSI_FG_YELLOW;
constexpr unsigned int SMG_REND_TAB_ACTIVE = ANSI_REVERSE | ANSI_BOLD;
constexpr unsigned int SMG_REND_TAB_INACT  = SMG_REND_NORMAL;
constexpr unsigned int SMG_REND_HELP_KEY   = ANSI_FG_CYAN;
constexpr unsigned int SMG_REND_TREE_LINE  = ANSI_FG_BLUE;
constexpr unsigned int SMG_REND_DIALOG_BTN = ANSI_REVERSE;
constexpr unsigned int SMG_REND_DIM        = ANSI_FG_BLUE;

// CPU bar renditions
constexpr unsigned int SMG_REND_CPU_USER   = ANSI_FG_GREEN;
constexpr unsigned int SMG_REND_CPU_SYSTEM = ANSI_FG_RED;
constexpr unsigned int SMG_REND_MEM_BAR    = ANSI_FG_CYAN;
constexpr unsigned int SMG_REND_SWAP_BAR   = ANSI_FG_YELLOW;

// Process state renditions
constexpr unsigned int SMG_REND_PROC_RUNNING  = ANSI_FG_GREEN;
constexpr unsigned int SMG_REND_PROC_SLEEPING = SMG_REND_NORMAL;
constexpr unsigned int SMG_REND_PROC_ZOMBIE   = ANSI_FG_RED;
constexpr unsigned int SMG_REND_PROC_STOPPED  = ANSI_FG_YELLOW;

// Get rendition for a process state character
unsigned int get_state_rendition(char state);

} // namespace pex
