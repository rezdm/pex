# HOW-TO: run pex with elevated privileges

pex works without any special privileges: you always get the full process tree
with CPU and memory for every process. Elevation is only needed to see the
*details* of **other users' processes** (open files, network, environment,
memory maps, I/O) and to kill them. Without it those panels just show
"access denied" / stay empty for foreign processes.

| OS | Mechanism | Scope of elevation | GUI-friendly |
|---|---|---|---|
| Linux | file capabilities (`setcap`) | 3 capabilities, this binary only | yes — works on Wayland, unlike sudo |
| Solaris | RBAC profile + `pfexec` | 3 privileges, this binary + assigned users only | yes |
| FreeBSD | `sudo` / `doas` | full root for the session | X11 only, see caveat |

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

> Why not `sudo pex`? On GNOME/Wayland root GUI clients are blocked or
> fragile, and sudo would write root-owned files into your `~/.config/pex`
> and `imgui.ini`. With capabilities the process stays your user.

### Option 1 — simple (single-user machine)

1. Set the capabilities:
   ```bash
   sudo setcap 'cap_sys_ptrace,cap_dac_read_search,cap_kill+ep' /opt/pex/pex
   sudo setcap 'cap_sys_ptrace,cap_dac_read_search,cap_kill+ep' /opt/pex/pexc   # if you use the TUI
   ```
2. Verify:
   ```bash
   getcap /opt/pex/pex
   # /opt/pex/pex cap_dac_read_search,cap_kill,cap_sys_ptrace=ep
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
   sudo setcap 'cap_sys_ptrace,cap_dac_read_search,cap_kill+ep' /opt/pex/pex
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
