#pragma once

// Curses backend selector: ncurses on unix, PDCursesMod on Windows (#43).
#ifdef _WIN32
#include <curses.h>  // PDCursesMod
// PDCursesMod provides the ncurses-compatible MEVENT API as nc_getmouse()
#ifndef getmouse
#define getmouse(ev) nc_getmouse(ev)
#endif
#else
#include <ncurses.h>
#endif
