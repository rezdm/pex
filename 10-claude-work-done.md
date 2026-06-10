# Work Done (Claude, 2026-06-09)

Branch: `feature/code-review-fixes`. Each item references the corresponding finding in
`00-claude-code-review.md`.

## Fixed

### Finding 1 — Socket inodes as `int`
`NetworkConnectionInfo::inode` is now `std::optional<uint64_t>`; `parse_net_file` and
the `socket:[...]` symlink scan parse into `uint64_t`. Connections with inode > 2^31
are no longer silently dropped.
Files: `src/core/model/process_info.hpp`, `src/platform/linux/procfs_reader.hpp`,
`src/platform/linux/procfs/procfs_network.cpp`.

### Finding 2 — Wrong network namespace
`get_network_connections` now reads `/proc/<pid>/net/{tcp,tcp6,udp,udp6}` (the target
process's namespace) instead of `/proc/net/*` (pex's own). Containerized processes now
show their connections.
File: `src/platform/linux/procfs/procfs_network.cpp`.

### Finding 3 — Solaris library sizes round-tripped through display strings
`MemoryMapInfo` gained `uint64_t size_bytes` (authoritative numeric value; the `size`
string is display-only and now produced by the shared `format_bytes`). Solaris
`get_libraries` uses `size_bytes` directly; the lossy `parse_size_string` helper was
deleted. All three backends (Linux/FreeBSD/Solaris) populate `size_bytes` and use
`format_bytes` instead of four hand-rolled copies of the same formatting.

### Finding 4 — Library sort by unpadded hex string
`LibraryInfo` gained `uint64_t base_addr` (numeric). FreeBSD and Solaris now sort by
it numerically; the `base_address` hex string remains for display.

### Finding 5 — FreeBSD executable path from argv[0]
New `get_executable_path()` helper uses `sysctl KERN_PROC_PATHNAME` to get the true
vnode path; argv[0] is only a fallback. Also used to identify the main executable in
`get_libraries`. Note: this adds one sysctl per process per refresh on FreeBSD; if it
ever shows up in profiles it can be cached keyed by (pid, start_time).

### Finding 6 — Inconsistent field semantics
- `priority`: FreeBSD now reports `ki_pri.pri_level`, Solaris `pr_pri` (both
  previously reported the nice value) — consistent with Linux's dynamic priority.
- `is_executable`: FreeBSD compares against the real executable path; Solaris uses
  the `a.out` mapping name (and substitutes the resolved `/proc/<pid>/path/a.out`
  path for display). Both previously meant "has any exec mapping".
- `resident_size`: Linux now populates real per-library RSS by reading
  `/proc/<pid>/smaps` (falls back to `maps` when unreadable). Solaris sets 0
  (= unknown; prmap_t has no RSS) instead of duplicating total_size.
- Error retention: FreeBSD/Solaris now match Linux (cap 10 entries, 10-second
  window in `get_recent_errors`).

### Finding 7 — Weak stat validation
`procfs_processes.cpp`: the parse-failure check is now `if (iss.fail())` instead of
`if (iss.fail() && state.empty())`, so a partially-parsed stat line is rejected
rather than silently producing zeroed fields.

### Finding 8 — Lost-wakeup race in DataStore
`stop()`, `refresh_now()`, `resume()` and `set_refresh_interval()` now write their
wake-up flags while holding `cv_mutex_` before notifying, eliminating the window
where the collection thread misses the notify and sleeps a full interval. `start()`
uses `exchange` to be idempotent. A comment documents the locking rule.
File: `src/core/services/data_store.cpp`.

### Finding 9 — SingleInstance teardown race
The destructor now: `shutdown()` the socket (wakes `accept()`), **joins** the
listener thread, and only then `close()`s the fd — the fd value never changes while
the listener can observe it, removing both the data race and the fd-reuse hazard.
The startup TOCTOU (two simultaneous instances) is documented as accepted.
File: `src/core/services/single_instance.cpp`.

### Finding 10 — Snapshot mutation invariant
Dead `ProcessNode::clone()` removed. The `DataSnapshot` invariant comment now
documents the one sanctioned exception (UI thread syncing `is_expanded` after
publication) instead of claiming full immutability.
Files: `src/core/services/data_store.cpp/.hpp`.

### Finding 12 — TUI constant CPU burn
The TUI main loop now uses `timeout(50)` blocking `getch` as its pacing mechanism
(no more `nodelay` + 16 ms sleep) and re-renders only when input was consumed, a
new data snapshot arrived, or the terminal was resized. Idle CPU drops from ~60
full redraws/second to zero.
File: `src/ui/tui/tui_app.cpp`.

### Finding 13 — Per-CPU counter underflow & sysconf in hot loops
- `DataStore::collect_data` guards both the system-wide and per-CPU deltas against
  counter regression (CPU hotplug) before subtracting unsigned counters.
- Linux: `sysconf(_SC_PAGESIZE)` hoisted to a function-local static.
- Solaris: `sysconf(_SC_CLK_TCK)` cached in the provider constructor.

### Finding 16 — Misc
- Solaris: unused `fd_num` stoi replaced with an explicit `from_chars` numeric-name
  filter.
- `SystemInfo::get_load_average` parses with `std::from_chars` (no exceptions on
  malformed `/proc/loadavg`).
- `.gitignore`: added `cmake-build-*/` (the IDE build dir was untracked but not
  ignored).
- Search (GUI **and** TUI): now matches the command line as well as the name, and
  searches the *entire* tree — matches under collapsed parents are found and their
  ancestor chain is auto-expanded (with cycle guards). GUI: `imgui_input.cpp`;
  TUI: `tui_app_navigation.cpp`.

### Finding 15 — OpenVMS removal (owner decision)
Deleted: `src/platform/openvms/`, `src/ui/tui_vms/`, `src/main_tui_vms.cpp`,
`DESCRIP.MMS`, `tools/` (VMS helper scripts). All `#ifdef __VMS` fallbacks stripped
from `data_store.cpp/.hpp` and `format_utils.hpp`; the `PEX_MUTEX` macros and the
VMS-only `thread_exited_` detach logic are gone. Core is plain C++23 again.

## Accepted / not changed (with rationale)

- **Kill's 100 ms sleep on the UI thread** (`linux_process_killer.cpp`): one frame
  hitch per kill action; moving kills to a worker thread adds lifecycle complexity
  disproportionate to the benefit for a desktop tool.
- **Solaris `pfiles`/`pargs` popen on the UI thread**: `pfiles` briefly stops the
  target and can be slow, but it is the documented fallback path only; making the
  details fetch async is a larger refactor, noted as a known limitation in the code.
- **FreeBSD TCP state inference** ("has peer ⇒ ESTABLISHED"): exact state needs the
  `net.inet.tcp.pcblist` sysctl walk; left as-is with the existing comment.
- **GUI list view re-sorting every frame**: negligible at desktop process counts.

## Tests — recommendation (finding 14, per owner question)

You don't need a test *framework* for a UI-centric one-off tool, and I have not
added one. But the bugs found in this review cluster in pure, UI-free code (parsers,
tree building, delta math) — exactly the code that broke during the FreeBSD/Solaris
ports. If regressions start costing you time, the cheap middle ground is:

1. One `pex_tests` executable built behind `-DBUILD_TESTS=ON`, plain `assert`-based,
   no external dependency.
2. Cover only the pure functions: `format_bytes`, the `/proc/.../stat` comm parsing
   (names with spaces/parens), `parse_net_file` hex parsing (run it on a string
   fixture by extracting the parser to take an `istream`), and `DataStore` tree
   building with a tiny fake `IProcessDataProvider`.
3. Add one `ctest` line to the existing GitHub workflow.

That's an afternoon of work and would have caught most of findings 1, 3, and 7
automatically. Until then, the CI build-only check is a reasonable floor.

## Verification

- Linux (WSL Debian 13/trixie, g++, Release): full configure + build of both `pex`
  (GUI) and `pexc` (TUI) succeeded with zero warnings (`-Wall -Wextra -Wpedantic`).
- `pexc` smoke-tested in a pseudo-terminal: starts, collects data (66 tasks / 24
  CPUs reported), renders the system panel and process list, exits cleanly.
- Solaris 11.4 (real hardware, GCC 14.2, system ncurses 6.4 via
  `-DCURSES_INCLUDE_DIR=/usr/include/ncurses -DCURSES_LIBRARY=/usr/lib/64/libncursesw.so.6`,
  `-DBUILD_GUI=OFF`): `pexc` builds with zero warnings and was smoke-tested live —
  system panel, process tree, thread counts and details panel all render; clean
  quit. One pre-existing portability bug surfaced and was fixed: `BUTTON5_PRESSED`
  (wheel-down) only exists with ncurses mouse protocol v2, now feature-guarded
  (`tui_input_panels.cpp`). The GUI was not built there (GLFW requires Xinerama
  headers, not installed on that box).
- FreeBSD backend could not be compiled (no FreeBSD machine available); changes
  there were kept minimal-diff and reviewed against the documented APIs. Watch CI /
  the VirtualBox FreeBSD setup on first build.
