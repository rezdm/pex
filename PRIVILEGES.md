# HOW-TO: run pex with elevated privileges

pex works without any special privileges: you always get the full process tree
with CPU and memory for every process. Elevation is only needed to see the
*details* of **other users' processes** (open files, network, environment,
memory maps, I/O), to kill them, and — on Linux, and more completely on
macOS/BSD — to subscribe to the kernel process-event feed behind the "Churn"
line. Without it those panels just show "access denied" / stay empty for
foreign processes, and pex collects data by polling only.

| OS | Mechanism | Scope of elevation | GUI-friendly |
|---|---|---|---|
| Linux | file capabilities (`setcap`) | 4 capabilities, this binary only | yes — works on Wayland, unlike sudo |
| Solaris | RBAC profile + `pfexec` | 3 privileges, this binary + assigned users only | yes |
| FreeBSD | `sudo` / `doas` | full root for the session | X11 only, see caveat |
| macOS | `sudo` (no capabilities/RBAC) | full root — but SIP still hides Apple-signed processes | yes, but see SIP caveat |

Do **not** make the binary setuid root: it would run the whole GUI/rendering
stack as root, which is exactly what the recipes below avoid.

Throughout, `/opt/pex/pex` and `/opt/pex/pexc` stand for wherever you
installed the binaries — adjust paths.

---

## Linux (Debian)

Capabilities used:

* `cap_sys_ptrace` — read `/proc/<pid>/{environ,maps,io,fd,exe}` of other users' processes
* `cap_dac_read_search` — bypass file-permission checks on procfs entries
* `cap_kill` — send signals to other users' processes. **Optional**: drop it
  from the commands below if you only want to observe, never kill.
* `cap_net_admin` — subscribe to the kernel process-event feed (proc
  connector), which powers the "Churn" line in the system panel and counts
  short-lived processes that polling never sees. **Optional**: drop it from
  the commands below if you don't want the event feed; pex then runs
  poll-only.

> Why not `sudo pex`? On GNOME/Wayland root GUI clients are blocked or
> fragile, and sudo would write root-owned files into your `~/.config/pex`
> and `imgui.ini`. With capabilities the process stays your user.

### Option 1 — simple (single-user machine)

1. Set the capabilities:
   ```bash
   sudo setcap 'cap_sys_ptrace,cap_dac_read_search,cap_kill,cap_net_admin+ep' /opt/pex/pex
   sudo setcap 'cap_sys_ptrace,cap_dac_read_search,cap_kill,cap_net_admin+ep' /opt/pex/pexc   # if you use the TUI
   ```
2. Verify:
   ```bash
   getcap /opt/pex/pex
   # /opt/pex/pex cap_dac_read_search,cap_kill,cap_net_admin,cap_sys_ptrace=ep
   ```
3. Run `pex` normally (no sudo). Select a root-owned process — the Files /
   Network / Environment tabs should now be populated.

Caveat: the capabilities apply to **anyone who can execute the file**. On a
machine with other human accounts, use Option 2.

Note: `setcap` must be re-applied after every rebuild/reinstall of the binary
(the capability lives in the file's extended attributes).

### Option 2 — restricted to a group (shared machine)

1. Create a group and add yourself:
   ```bash
   sudo groupadd pexusers
   sudo usermod -aG pexusers $USER
   ```
2. Log out and back in (or `newgrp pexusers`) so the group membership is active.
3. Restrict execution of the binary to the group:
   ```bash
   sudo chown root:pexusers /opt/pex/pex
   sudo chmod 750 /opt/pex/pex
   ```
4. Set the capabilities (same as Option 1):
   ```bash
   sudo setcap 'cap_sys_ptrace,cap_dac_read_search,cap_kill,cap_net_admin+ep' /opt/pex/pex
   ```
5. Verify: `pex` runs and shows foreign-process details for you; another
   (non-member) user gets "Permission denied" when executing it.

Repeat steps 3–4 for `/opt/pex/pexc` if wanted.

Extra notes:
* Capability-tagged binaries run in secure-exec mode (`LD_PRELOAD` etc. are
  ignored) — that is a feature, not a bug.
* If `/proc` is mounted with `hidepid=1/2`, visibility of foreign processes is
  decided by the mount's `gid=` option; capabilities do not override `hidepid`.

---

## Solaris

Privileges used:

* `proc_owner` — inspect and signal other users' processes
* `file_dac_read`, `file_dac_search` — read procfs entries regardless of permissions

### Option 1 — RBAC execution profile + pfexec (recommended)

Privileges apply only when running these exact binaries via `pfexec`.

1. As root, define a rights profile — create `/etc/security/prof_attr.d/pex`
   with the line:
   ```
   Pex Monitor:::Run pex with process observation privileges:
   ```
2. Attach the privileges to the pex binaries — create
   `/etc/security/exec_attr.d/pex` with one line per binary:
   ```
   Pex Monitor:solaris:cmd:RO::/opt/pex/pex:privs=proc_owner,file_dac_read,file_dac_search
   Pex Monitor:solaris:cmd:RO::/opt/pex/pexc:privs=proc_owner,file_dac_read,file_dac_search
   ```
   (The `exec_attr` lines are what actually carry the privileges — a
   `prof_attr` entry alone grants nothing. The path must match exactly.)
3. Assign the profile to your user:
   ```bash
   usermod -P +'Pex Monitor' <your-user>
   ```
4. Verify the assignment:
   ```bash
   profiles <your-user>        # should list: Pex Monitor
   getent prof_attr "Pex Monitor"
   ```
5. Run via pfexec:
   ```bash
   pfexec /opt/pex/pexc
   ```
   To double-check the privileges took effect, run `ppriv <pid-of-pexc>` from
   another terminal — the E (effective) set should include `proc_owner`.

(The pex Solaris backend also shells out to `pfiles`/`pargs` as a fallback;
those child processes inherit the privileges under `pfexec` automatically.)

### Option 2 — default privileges for a user (personal box)

Grants the privileges to **every process** of the user — no `pfexec` needed,
but every program you run carries them. Prefer Option 1 on shared systems.

1. As root:
   ```bash
   usermod -K defaultpriv=basic,proc_owner,file_dac_read,file_dac_search <your-user>
   ```
2. Log out and back in.
3. Verify: `ppriv $$` — the E set should include `proc_owner`.
4. Run `pex` / `pexc` normally.

To undo: `usermod -K defaultpriv=basic <your-user>`.

---

## FreeBSD

FreeBSD has no file capabilities (Capsicum only *drops* rights) and no RBAC —
reading other users' file descriptors and environment genuinely requires
root. Use `sudo` or `doas`.

### TUI

```bash
sudo pexc
```
or, with `doas` (`pkg install doas`), add to `/usr/local/etc/doas.conf`:
```
permit persist <your-user> as root cmd /opt/pex/pexc
```
then:
```bash
doas /opt/pex/pexc
```

### GUI (X11)

Root needs your X authority to open the display:
```bash
sudo -E pex
```
If that fails with "cannot open display", pass the authority explicitly:
```bash
sudo XAUTHORITY=$HOME/.Xauthority DISPLAY=$DISPLAY pex
```

Footnotes:
* `security.bsd.see_other_uids=1` (the default) is what makes other users'
  processes visible at all; hardened systems set it to 0, which hides them
  from non-root regardless of the above.
* Settings saved while running under sudo land in **root's**
  `~/.config/pex/pex.conf` (and root's `imgui.ini`), not yours.

---

## macOS

macOS has no file capabilities and no RBAC for this; reading other users'
open files, network sockets, environment (`KERN_PROCARGS2`), and memory maps
requires root. Use `sudo`.

But root is **not** the whole story here — **System Integrity Protection
(SIP)** and the **hardened runtime / AMFI** cap what any process, even root,
may inspect:

* `task_for_pid()` (needed to read another process's threads for stack
  unwinding) is denied to any binary lacking a debugger entitlement — **root
  does not lift this**. It's not only SIP protecting Apple-signed targets:
  AMFI/the hardened runtime blocks it for arbitrary targets too. Granting it
  would require code-signing pex with `com.apple.security.cs.debugger` /
  `get-task-allow`, which needs an Apple Developer ID or local ad-hoc signing —
  infrastructure this project doesn't ship. So **per-thread stack traces are
  unavailable on macOS regardless of `sudo`**, by design, not as a bug. pex
  never calls `task_for_pid()` — it reads everything else through `libproc` and
  `sysctl` — so it degrades gracefully; but some details of system daemons also
  stay hidden even under `sudo`.
* `KERN_PROCARGS2` (the argv/env source) only returns another process's
  arguments to root; unprivileged, you see full argv/env for **your own**
  processes and just the name/path for others.
* The "Churn" line is fed by a `kqueue`/`EVFILT_PROC` watch. Without root it
  can only attach to processes you already own, so foreign short-lived
  processes are missed; the feed is best-effort and pex still polls.

### TUI

```bash
sudo pexc
```

### GUI (Metal)

Unlike Wayland, a Terminal-launched GUI runs fine as root on macOS:

```bash
sudo /opt/pex/pex
```

Caveats:
* Settings and `imgui.ini` saved while running under `sudo` land in **root's**
  home (`/var/root/…`), not yours.
* The "proper" alternative to `sudo` — code-signing the binary with a
  debugging entitlement (`com.apple.security.get-task-allow` and friends) —
  needs an Apple provisioning profile and still won't lift SIP on Apple-signed
  targets, so it buys little for a self-built tool. `sudo` is the pragmatic
  choice.

Without any elevation pex is still fully useful: the complete process tree
with CPU/memory, and full detail for **your own** processes.

---

## Nuclear alternative: setuid root (works everywhere, recommended nowhere)

For completeness — it works on all three OSes:

```bash
sudo chown root /opt/pex/pexc
sudo chmod u+s /opt/pex/pexc
```

Understand what you are buying: pex needs privileged reads on **every refresh
tick**, so unlike a well-behaved setuid program it cannot elevate briefly and
drop — the *entire* application holds root for its whole lifetime. That root
is then held by code that was never written to be privilege-safe:

* the **DNS resolver** — the network tab does reverse lookups of remote IPs
  via `getnameinfo()`, i.e. a root process parsing hostile network data;
* **ncurses** (TUI) — a long CVE history of parsing terminal/terminfo data;
* **Mesa/GL drivers + GLFW + ImGui** (GUI) — a huge attack surface, and the
  same root-on-Wayland problems as sudo anyway.

A compromise of a `setcap` pex yields four capabilities; a compromise of a
setuid pex yields the machine. On Linux and Solaris setuid buys *zero*
functionality over the recipes above — only blast radius. The only place it
is even arguable is FreeBSD (no finer mechanism exists), and there:

* if you must, make only **`pexc`** setuid (no GL stack), never the GUI;
* anyone who can execute the file gets root-powered pex — combine with the
  group-restriction pattern from the Linux section (`chown root:pexusers`,
  `chmod 4750`);
* the *traditional* narrow BSD answer would be setgid `kmem` with a
  libkvm-based backend (how `ps`/`top` historically worked) — pex's FreeBSD
  backend currently uses sysctls, so this would require code changes; noted
  here as the principled future option.

To undo: `sudo chmod u-s /opt/pex/pexc`.
