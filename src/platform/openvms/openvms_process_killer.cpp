#include "openvms_process_killer.hpp"

#include <cstring>
#include <vector>
#include <set>

// OpenVMS system headers
#include <starlet.h>
#include <lib$routines.h>
#include <descrip.h>
#include <jpidef.h>
#include <pscandef.h>
#include <ssdef.h>
#include <stsdef.h>
#include <efndef.h>
#include <iosbdef.h>
#include <iledef.h>

namespace pex {

KillResult OpenVMSProcessKiller::kill_process(int pid, bool force) {
    KillResult result;

    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }

    unsigned int vms_pid = static_cast<unsigned int>(pid);
    unsigned int status;

    if (force) {
        // $DELPRC: Immediate process deletion without running exit handlers.
        // This is the VMS equivalent of SIGKILL.
        status = sys$delprc(&vms_pid, nullptr);
    } else {
        // $FORCEX: Force exit - queues an AST that calls $EXIT, allowing
        // exit handlers to run. This is the VMS equivalent of SIGTERM.
        status = sys$forcex(&vms_pid, nullptr, SS$_FORCEDEXIT);
    }

    if ($VMS_STATUS_SUCCESS(status)) {
        result.success = true;

        if (!force) {
            // For graceful exit, check if the process is still running.
            // $FORCEX is asynchronous - the process may still be executing
            // its exit handlers.
            unsigned int check_state = 0;
            ILE3 check_items[] = {
                { sizeof(check_state), JPI$_STATE, &check_state, nullptr },
                { 0, 0, nullptr, nullptr }
            };
            IOSB iosb;
            unsigned int check_pid = vms_pid;
            unsigned int check_status = sys$getjpiw(EFN$C_ENF, &check_pid,
                                                     nullptr, check_items,
                                                     &iosb, nullptr, 0);
            if ($VMS_STATUS_SUCCESS(check_status) &&
                $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
                result.process_still_running = true;
                result.error_message = "Exit requested. Process may still be "
                                       "running exit handlers. Use Force Kill "
                                       "($DELPRC) if it doesn't terminate.";
            } else {
                result.process_still_running = false;
            }
        } else {
            result.process_still_running = false;
        }
        return result;
    }

    // Handle error conditions
    switch (status) {
        case SS$_NONEXPR:
            result.success = false;
            result.error_message = "Process not found. It may have already terminated.";
            break;
        case SS$_NOPRIV:
            result.success = false;
            result.error_message = "Insufficient privilege. You need GROUP or "
                                   "WORLD privilege to affect this process.";
            break;
        default:
            result.success = false;
            result.error_message = "Failed to terminate process (VMS status: " +
                                   std::to_string(status) + ").";
            break;
    }

    return result;
}

KillResult OpenVMSProcessKiller::kill_process_tree(int pid, bool force) {
    KillResult result;

    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }

    // Build a set of all PIDs to kill by finding descendants.
    // On VMS, subprocess relationships are tracked via JPI$_OWNER (parent PID).
    std::set<unsigned int> pids_to_kill;
    pids_to_kill.insert(static_cast<unsigned int>(pid));

    // Scan all processes, building a (pid -> owner) map
    struct ProcEntry {
        unsigned int pid;
        unsigned int owner;
        int64_t logintim;  // For PID reuse verification
    };
    std::vector<ProcEntry> all_procs;

    unsigned int scan_ctx = 0;
    struct {
        unsigned short length;
        unsigned short code;
        unsigned int value;
        unsigned int flags;
    } scan_items[] = {
        { 0, 0, 0, 0 }
    };

    unsigned int status = sys$process_scan(&scan_ctx, scan_items);
    if (!$VMS_STATUS_SUCCESS(status)) {
        result.success = false;
        result.error_message = "Failed to scan processes.";
        return result;
    }

    unsigned int proc_pid = 0;
    unsigned int proc_owner = 0;
    int64_t proc_logintim = 0;

    ILE3 jpi_items[] = {
        { sizeof(proc_pid),      JPI$_PID,      &proc_pid,      nullptr },
        { sizeof(proc_owner),    JPI$_OWNER,    &proc_owner,    nullptr },
        { sizeof(proc_logintim), JPI$_LOGINTIM, &proc_logintim, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;

    while (true) {
        proc_pid = 0;
        proc_owner = 0;
        proc_logintim = 0;

        status = sys$getjpiw(EFN$C_ENF, &scan_ctx, nullptr,
                             jpi_items, &iosb, nullptr, 0);

        if (iosb.iosb$l_getxxi_status == SS$_NOMOREPROC) {
            break;
        }
        if (!$VMS_STATUS_SUCCESS(status) || !$VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
            continue;
        }

        all_procs.push_back({ proc_pid, proc_owner, proc_logintim });
    }

    // Find all descendants iteratively
    bool found_new = true;
    while (found_new) {
        found_new = false;
        for (const auto& proc : all_procs) {
            if (pids_to_kill.count(proc.owner) && !pids_to_kill.count(proc.pid)) {
                pids_to_kill.insert(proc.pid);
                found_new = true;
            }
        }
    }

    // Kill all processes in the tree (children first, then parent).
    // Reverse order so children are killed before their parents.
    std::vector<unsigned int> sorted_pids(pids_to_kill.begin(), pids_to_kill.end());

    bool any_success = false;
    bool any_nopriv = false;

    for (auto it = sorted_pids.rbegin(); it != sorted_pids.rend(); ++it) {
        unsigned int kill_pid = *it;

        if (force) {
            status = sys$delprc(&kill_pid, nullptr);
        } else {
            status = sys$forcex(&kill_pid, nullptr, SS$_FORCEDEXIT);
        }

        if ($VMS_STATUS_SUCCESS(status)) {
            any_success = true;
        } else if (status == SS$_NOPRIV) {
            any_nopriv = true;
        }
    }

    if (any_success) {
        result.success = true;
        if (!force) {
            // Check if root process still exists
            unsigned int check_state = 0;
            ILE3 check_items[] = {
                { sizeof(check_state), JPI$_STATE, &check_state, nullptr },
                { 0, 0, nullptr, nullptr }
            };
            unsigned int check_pid = static_cast<unsigned int>(pid);
            unsigned int check_status = sys$getjpiw(EFN$C_ENF, &check_pid,
                                                     nullptr, check_items,
                                                     &iosb, nullptr, 0);
            if ($VMS_STATUS_SUCCESS(check_status) &&
                $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
                result.process_still_running = true;
                result.error_message = "Exit requested for process tree. "
                                       "Processes may still be running exit handlers.";
            } else {
                result.process_still_running = false;
            }
        } else {
            result.process_still_running = false;
        }
        return result;
    }

    if (any_nopriv) {
        result.success = false;
        result.error_message = "Insufficient privilege. You need GROUP or "
                               "WORLD privilege to affect these processes.";
    } else {
        result.success = false;
        result.error_message = "Process not found. It may have already terminated.";
    }

    return result;
}

} // namespace pex
