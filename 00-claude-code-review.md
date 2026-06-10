# PEX Code Review (Claude, 2026-06-09)

Full review of the codebase: core services, Linux/FreeBSD/Solaris backends, ImGui GUI,
ncurses TUI. The OpenVMS port was only skimmed (parked per commit b1bdd95).

**Overall verdict:** well-architected codebase — clean ports-and-adapters layering,
separate provider instances for the collection thread vs. UI thread, immutable shared
snapshots, PID-reuse guards in the kill path, documented lifetime invariants. The
findings below are mostly second-order.

---

## Correctness bugs

### 1. Socket inodes stored as `int` — connections silently dropped
`NetworkConnectionInfo::inode` is `std::optional<int>` (`process_info.hpp:69`) and
`parse_net_file` parses into `int` (`procfs_network.cpp:74`). Linux socket inodes are
unsigned long; on a long-uptime busy box they exceed 2^31, `from_chars` fails leaving
`inode == 0`, and the row is skipped. Same truncation when matching `socket:[...]`
links at `procfs_network.cpp:113-117`. Should be `uint64_t` throughout.

### 2. Wrong network namespace
`get_network_connections` reads `/proc/net/tcp` etc. (`procfs_network.cpp:129-132`) —
that is *pex's own* netns. For any process in a container/netns (docker, systemd
`PrivateNetwork`), the inode lookup finds nothing. Should read `/proc/<pid>/net/tcp`.

### 3. Solaris library sizes reconstructed from formatted strings
`get_libraries` calls `get_memory_maps` and then `parse_size_string("1.2 MB")` to
recover bytes (`solaris_process_details.cpp:256, 639`). Lossy round-trip through the
*display* string — sizes can be off by several percent. `MemoryMapInfo` should carry a
numeric size.

### 4. Library sort by hex string is wrong
FreeBSD and Solaris sort libraries by `base_address` as a *string*
(`freebsd_process_details.cpp:396`, `solaris_process_details.cpp:672`), but
`base_address` is formatted with `{:x}` without zero-padding — so `"f0000000" >
"10000000000"` lexicographically. Needs numeric sort key or zero-padding.

### 5. FreeBSD `executable_path` is argv[0]
`freebsd_process_data_provider.cpp:159-161` uses `args[0]`, which any process can set
to anything. `sysctl KERN_PROC_PATHNAME` returns the real vnode path.

### 6. Inconsistent field semantics across platforms
- `priority`: Linux stores dynamic priority from `/proc/stat`, FreeBSD stores
  `ki_nice`, Solaris stores `pr_nice`. Same column, three meanings.
- `is_executable` on `LibraryInfo`: Linux means "is the main executable",
  FreeBSD/Solaris mean "has an exec mapping" (true for nearly every lib) — the
  "main exe first" sort in the libraries tab only works on Linux.
- `resident_size`: never populated on Linux (TUI displays it at
  `tui_details_tabs.cpp:380` — always 0), and on Solaris it is a copy of total.
- Error retention: Linux keeps 10 errors / 10-second window; FreeBSD/Solaris keep 100
  forever. Status bar behaves differently per platform.

### 7. Weak stat validation
`procfs_processes.cpp:89`: `if (iss.fail() && state.empty())` — a stat line that
parses only the state char still passes with zeroed ppid/utime/stime. Intended `||`
or per-field validation.

---

## Concurrency

### 8. Lost-wakeup race in `DataStore`
`refresh_now()` and `stop()` set an atomic flag and call `cv_.notify_all()` *without
holding `cv_mutex_`* (`data_store.cpp:50-54, 89-96`). The collection thread can
evaluate the predicate (false), get preempted, miss the notify, and only wake at the
next timeout — "refresh now" and shutdown can stall up to the full refresh interval
(5 s at the slowest setting). Lock `cv_mutex_` around the flag-set.

### 9. `SingleInstance` teardown race
The destructor closes `server_fd_` while the listener thread may be entering
`accept()` on it (`single_instance.cpp:13-30, 103-111`). `server_fd_` is a plain `int`
written by one thread and read by another, plus the classic
close-while-another-thread-uses-fd reuse hazard. Safe pattern: `shutdown()` to wake
the accept, join the thread, *then* close. Also a startup TOCTOU: two instances
launched simultaneously can both fail `connect()`, both `unlink()`+`bind()`, and both
become primary (acceptable for this tool, but worth a comment).

### 10. UI writes into the shared snapshot
`imgui_app.cpp:137-139` sets `node->is_expanded` on snapshot nodes, violating the
documented invariant "never modified after construction" (`data_store.hpp:41-44`).
Currently benign (only the UI touches a published snapshot), but the source of truth
already exists in `collapsed_pids`. Related: `ProcessNode::clone()`
(`data_store.cpp:13`) is dead code — nothing calls it.

### 11. Blocking work on the UI thread
`kill_process` sleeps 100 ms inline (`linux_process_killer.cpp:151`) → a visible frame
hitch on every SIGTERM. On Solaris the details panel shells out to `pfiles`/`pargs`
via `popen` (`solaris_process_details.cpp:96, 588`); `pfiles` briefly *stops the
target process* and can take seconds on processes with many fds — all on the render
thread. (Tracked as [#31](https://github.com/rezdm/pex/issues/31).)

---

## Performance

### 12. The TUI burns CPU constantly
`tui_app.cpp:109-139` re-erases and re-renders every window every ~16 ms, forever,
even when nothing changed — a process monitor that shows itself near the top of its
own list. Fix: render only when input arrived, data changed, or a resize happened,
and use `timeout()`-based blocking `getch` instead of `nodelay` + sleep.

### 13. Smaller ones
- `sysconf(_SC_CLK_TCK)` per process per tick on Solaris
  (`solaris_process_data_provider.cpp:104`)
- `sysconf(_SC_PAGESIZE)` per process per tick on Linux (`procfs_processes.cpp:114`)
- GUI list view re-sorts the full flat list every frame
  (`imgui_process_list_view.cpp:252`) — fine at desktop scale
- Per-CPU delta math has no monotonicity guard — a counter regression (CPU
  offline/online reorder) causes unsigned underflow → garbage percentages
  (`data_store.cpp:304-323`)

---

## Robustness / housekeeping

### 14. Zero tests
No `enable_testing()`, no test target; CI only verifies that binaries link. The most
bug-prone code is pure and trivially testable (stat comm parsing, `parse_net_file`
hex parsing, tree building/orphan handling, CPU-delta/PID-reuse logic,
`format_bytes`). See `10-claude-work-done.md` for the recommendation discussed with
the owner.

### 15. `#ifdef __VMS` leakage through shared code
`data_store.cpp/.hpp`, `format_utils.hpp` carry VMS fallbacks for a port declared
abandoned in commit b1bdd95. Every core change has to keep a dead platform compiling.
(Owner decision: remove the VMS port entirely.)

### 16. Misc
- `solaris_process_details.cpp:454-455`: `int fd_num = 0; fd_num = std::stoi(...)` —
  result never used.
- `SystemInfo::get_load_average` uses `std::stoi` uncaught (`system_info.cpp:145`);
  a malformed `/proc/loadavg` would terminate the app.
- Search (Ctrl+F) matches only the process *name* and, in tree view, only expanded
  nodes — Windows Process Explorer searches command lines and the whole tree.
- `cmake-build-debug/` is not tracked by git, but it is also not in `.gitignore`
  (only `build/` is) — IDE build dirs should be ignored explicitly.
- `tcp_states[0]` is the empty string (`procfs_network.cpp:59-62`) — state 0 is not
  valid in practice, cosmetic only.
- `MemoryInfo`/`SwapInfo` parsing reads into an uninitialized `int64_t value` if a
  `/proc/meminfo` line is malformed (`system_info.cpp:96-98`) — harmless in practice.

---

## Related open GitHub issues

Pre-existing issues that overlap with findings in this review:

- [#31 — Solaris: avoid command spawning (pfiles/pargs)](https://github.com/rezdm/pex/issues/31)
  — finding 11.
- [#26 — FreeBSD: accurate TCP state via net.inet.tcp.pcblist](https://github.com/rezdm/pex/issues/26)
  — the "has peer ⇒ ESTABLISHED" inference noted under finding 6's platform gaps.
- [#28 — Solaris: user/kernel time split](https://github.com/rezdm/pex/issues/28)
  — Solaris reports combined CPU time in `user_time` with `kernel_time = 0`.
- [#29 — Solaris: processor binding](https://github.com/rezdm/pex/issues/29)
  — threads tab shows last CPU (`pr_onpro`), not the binding (`pr_bindpro`).
- [#7 — Search by open file/socket](https://github.com/rezdm/pex/issues/7)
  — the search improvements in this review cover name + command line only.
