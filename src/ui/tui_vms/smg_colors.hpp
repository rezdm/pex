#pragma once

// SMG$ rendition mapping for OpenVMS TUI
//
// SMG$ supports these built-in renditions:
//   SMG$M_BOLD, SMG$M_REVERSE, SMG$M_UNDERLINE, SMG$M_BLINK
//
// Plus 8 user-defined renditions (SMG$M_USER1 through SMG$M_USER8)
// that can be mapped to terminal-specific video attributes via TERMTABLE.
// On VT-compatible terminals, these can be mapped to colors using
// ANSI SGR sequences in the terminal definition.
//
// Since we only have 8 user renditions, we map them to the most
// important visual distinctions. For the remaining cases, we fall
// back to combining built-in attributes (bold, reverse, underline).

#ifdef __VMS
#define __NEW_STARLET 1
#include <smgdef.h>
#else
// Stub definitions for non-VMS compilation
#define SMG$M_BOLD      0x00000001
#define SMG$M_REVERSE   0x00000002
#define SMG$M_BLINK     0x00000004
#define SMG$M_UNDERLINE 0x00000008
#define SMG$M_USER1     0x00000100
#define SMG$M_USER2     0x00000200
#define SMG$M_USER3     0x00000400
#define SMG$M_USER4     0x00000800
#define SMG$M_USER5     0x00001000
#define SMG$M_USER6     0x00002000
#define SMG$M_USER7     0x00004000
#define SMG$M_USER8     0x00008000
#endif

namespace pex {

// Rendition assignments for the 8 user-defined slots.
// TERMTABLE should map these to ANSI colors:
//   USER1 = green foreground  (running processes, CPU user bar)
//   USER2 = red foreground    (zombie, error, CPU system bar)
//   USER3 = cyan foreground   (title, selection highlight, memory bar)
//   USER4 = yellow foreground (header, warning, stopped, swap bar)
//   USER5 = blue foreground   (status bar background, tree lines, border)
//   USER6 = white on blue     (dialog background)
//   USER7 = black on cyan     (selected row)
//   USER8 = black on yellow   (search highlight)

// Named rendition constants for semantic use
constexpr unsigned int SMG_REND_NORMAL    = 0;
constexpr unsigned int SMG_REND_BOLD      = SMG$M_BOLD;
constexpr unsigned int SMG_REND_REVERSE   = SMG$M_REVERSE;
constexpr unsigned int SMG_REND_UNDERLINE = SMG$M_UNDERLINE;
constexpr unsigned int SMG_REND_BLINK     = SMG$M_BLINK;

// Color-mapped user renditions
constexpr unsigned int SMG_REND_GREEN     = SMG$M_USER1;       // Running, CPU user
constexpr unsigned int SMG_REND_RED       = SMG$M_USER2;       // Zombie, error, CPU system
constexpr unsigned int SMG_REND_CYAN      = SMG$M_USER3;       // Title, mem bar
constexpr unsigned int SMG_REND_YELLOW    = SMG$M_USER4;       // Header, warning, swap
constexpr unsigned int SMG_REND_BLUE      = SMG$M_USER5;       // Border, tree lines
constexpr unsigned int SMG_REND_DIALOG    = SMG$M_USER6;       // Dialog background
constexpr unsigned int SMG_REND_SELECTED  = SMG$M_USER7;       // Selected row
constexpr unsigned int SMG_REND_SEARCH    = SMG$M_USER8;       // Search highlight

// Compound renditions (combining built-in + user)
constexpr unsigned int SMG_REND_TITLE      = SMG_REND_CYAN | SMG_REND_BOLD;
constexpr unsigned int SMG_REND_HEADER     = SMG_REND_YELLOW | SMG_REND_BOLD;
constexpr unsigned int SMG_REND_STATUS     = SMG_REND_BLUE | SMG_REND_REVERSE;
constexpr unsigned int SMG_REND_ERROR      = SMG_REND_RED | SMG_REND_BOLD;
constexpr unsigned int SMG_REND_WARNING    = SMG_REND_YELLOW;
constexpr unsigned int SMG_REND_TAB_ACTIVE = SMG_REND_REVERSE | SMG_REND_BOLD;
constexpr unsigned int SMG_REND_TAB_INACT  = SMG_REND_NORMAL;
constexpr unsigned int SMG_REND_HELP_KEY   = SMG_REND_CYAN;
constexpr unsigned int SMG_REND_TREE_LINE  = SMG_REND_BLUE;
constexpr unsigned int SMG_REND_DIALOG_BTN = SMG_REND_REVERSE;
constexpr unsigned int SMG_REND_DIM        = SMG_REND_BLUE;     // Approximation for dim text

// CPU bar renditions
constexpr unsigned int SMG_REND_CPU_USER   = SMG_REND_GREEN;
constexpr unsigned int SMG_REND_CPU_SYSTEM = SMG_REND_RED;
constexpr unsigned int SMG_REND_MEM_BAR    = SMG_REND_CYAN;
constexpr unsigned int SMG_REND_SWAP_BAR   = SMG_REND_YELLOW;

// Process state renditions
constexpr unsigned int SMG_REND_PROC_RUNNING  = SMG_REND_GREEN;
constexpr unsigned int SMG_REND_PROC_SLEEPING = SMG_REND_NORMAL;
constexpr unsigned int SMG_REND_PROC_ZOMBIE   = SMG_REND_RED;
constexpr unsigned int SMG_REND_PROC_STOPPED  = SMG_REND_YELLOW;

// Get rendition for a process state character
unsigned int get_state_rendition(char state);

} // namespace pex
