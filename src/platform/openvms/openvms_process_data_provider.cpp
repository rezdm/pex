#include "openvms_process_data_provider.hpp"

#include <cstring>
#include <chrono>

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
#include <syidef.h>

// OpenVMS scheduler state constants.
// These are normally in <schdef.h> but that header may not be available
// on all OpenVMS versions/platforms. Define them manually if needed.
#ifndef SCH$C_CUR
#define SCH$C_CUR   1   // Currently executing
#define SCH$C_COM   3   // Computable
#define SCH$C_COMO  4   // Computable, outswapped
#define SCH$C_LEF   5   // Local event flag wait
#define SCH$C_LEFO  6   // LEF, outswapped
#define SCH$C_HIB   7   // Hibernating
#define SCH$C_HIBO  8   // Hibernating, outswapped
#define SCH$C_CEF   9   // Common event flag wait
#define SCH$C_SUSP  12  // Suspended
#define SCH$C_SUSPO 13  // Suspended, outswapped
#define SCH$C_PFW   14  // Page fault wait
#define SCH$C_FPG   15  // Free page wait
#define SCH$C_COLPG 16  // Collided page wait
// Resource wait states
#define SCH$C_RWAST  17
#define SCH$C_RWBRK  18
#define SCH$C_RWCAP  19
#define SCH$C_RWCLU  20
#define SCH$C_RWCSV  21
#define SCH$C_RWIMG  22
#define SCH$C_RWMBX  23
#define SCH$C_RWMPB  24
#define SCH$C_RWMPE  25
#define SCH$C_RWNPG  26
#define SCH$C_RWPFF  27
#define SCH$C_RWQUO  28
#define SCH$C_RWSCS  29
#define SCH$C_RWSWP  30
#endif

namespace pex {

// VMS epoch is Nov 17, 1858 00:00:00. Unix epoch is Jan 1, 1970 00:00:00.
// Difference in 100-nanosecond intervals.
static constexpr int64_t VMS_TO_UNIX_OFFSET = 35067168000000000LL;

// On Alpha and x86-64, a pagelet is 512 bytes.
static constexpr int VMS_PAGELET_SIZE = 512;

static std::chrono::system_clock::time_point vms_time_to_chrono(int64_t vms_time) {
    int64_t unix_100ns = vms_time - VMS_TO_UNIX_OFFSET;
    auto duration = std::chrono::duration<int64_t, std::ratio<1, 10000000>>(unix_100ns);
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(duration));
}

OpenVMSProcessDataProvider::OpenVMSProcessDataProvider() = default;
OpenVMSProcessDataProvider::~OpenVMSProcessDataProvider() = default;

void OpenVMSProcessDataProvider::add_error(const std::string& context,
                                           const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > 100) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

std::string OpenVMSProcessDataProvider::trim_vms_string(const char* str, int len) {
    // VMS strings are blank-padded; trim trailing spaces
    while (len > 0 && str[len - 1] == ' ') {
        --len;
    }
    return std::string(str, len);
}

char OpenVMSProcessDataProvider::map_state(unsigned int vms_state) {
    // Map VMS scheduling states to platform-neutral state characters.
    // VMS state constants are defined in <schdef.h> as SCH$C_*.
    switch (vms_state) {
        case SCH$C_CUR:    return 'R';  // Currently executing on a CPU
        case SCH$C_COM:    return 'R';  // Computable (ready to run)
        case SCH$C_COMO:   return 'R';  // Computable, outswapped

        case SCH$C_LEF:    return 'S';  // Local event flag wait
        case SCH$C_LEFO:   return 'S';  // LEF, outswapped
        case SCH$C_HIB:    return 'S';  // Hibernating
        case SCH$C_HIBO:   return 'S';  // Hibernating, outswapped
        case SCH$C_CEF:    return 'S';  // Common event flag wait

        case SCH$C_SUSP:   return 'T';  // Suspended
        case SCH$C_SUSPO:  return 'T';  // Suspended, outswapped

        case SCH$C_PFW:    return 'D';  // Page fault wait
        case SCH$C_FPG:    return 'D';  // Free page wait
        case SCH$C_COLPG:  return 'D';  // Collided page wait

        // Resource wait states
        case SCH$C_RWAST:  return 'D';
        case SCH$C_RWBRK:  return 'D';
        case SCH$C_RWCAP:  return 'D';
        case SCH$C_RWCLU:  return 'D';
        case SCH$C_RWCSV:  return 'D';
        case SCH$C_RWIMG:  return 'D';
        case SCH$C_RWMBX:  return 'D';
        case SCH$C_RWMPB:  return 'D';
        case SCH$C_RWMPE:  return 'D';
        case SCH$C_RWNPG:  return 'D';
        case SCH$C_RWPFF:  return 'D';
        case SCH$C_RWQUO:  return 'D';
        case SCH$C_RWSCS:  return 'D';
        case SCH$C_RWSWP:  return 'D';

        default:           return '?';
    }
}

std::vector<ProcessInfo> OpenVMSProcessDataProvider::get_all_processes(int64_t total_memory) {
    std::vector<ProcessInfo> processes;

    // Get total physical memory if not provided
    if (total_memory < 0) {
        unsigned int memsize = 0;
        unsigned short memsize_len = 0;
        ILE3 syi_items[] = {
            { sizeof(memsize), SYI$_MEMSIZE, &memsize, &memsize_len },
            { 0, 0, nullptr, nullptr }
        };
        IOSB syi_iosb;
        unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                          syi_items, &syi_iosb, nullptr, 0);
        if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(syi_iosb.iosb$l_getxxi_status)) {
            // memsize is in pagelets on x86-64
            total_memory = static_cast<int64_t>(memsize) * VMS_PAGELET_SIZE;
        }
    }

    // Create a wildcard process scan context to enumerate all processes.
    // An empty item list (terminator only) means scan all processes on this node.
    unsigned int scan_ctx = 0;
    struct {
        unsigned short length;
        unsigned short code;
        unsigned int value;
        unsigned int flags;
    } scan_items[] = {
        { 0, 0, 0, 0 }  // Terminator = wildcard scan
    };

    unsigned int status = sys$process_scan(&scan_ctx, scan_items);
    if (!$VMS_STATUS_SUCCESS(status)) {
        add_error("get_all_processes", "sys$process_scan failed with status " +
                  std::to_string(status));
        return processes;
    }

    // Buffers for $GETJPI item list
    unsigned int pid = 0;
    unsigned int owner_pid = 0;
    char prcnam[16] = {};
    unsigned short prcnam_len = 0;
    char username[13] = {};
    unsigned short username_len = 0;
    char imagname[256] = {};
    unsigned short imagname_len = 0;
    unsigned int state = 0;
    unsigned int cputim = 0;
    unsigned int pri = 0;
    unsigned int ppgcnt = 0;
    unsigned int gpgcnt = 0;
    unsigned int virtpeak = 0;
    unsigned int kt_count = 0;
    int64_t logintim = 0;

    ILE3 jpi_items[] = {
        { sizeof(pid),       JPI$_PID,       &pid,       nullptr },
        { sizeof(owner_pid), JPI$_OWNER,     &owner_pid, nullptr },
        { sizeof(prcnam),    JPI$_PRCNAM,    prcnam,     &prcnam_len },
        { sizeof(username),  JPI$_USERNAME,  username,   &username_len },
        { sizeof(imagname),  JPI$_IMAGNAME,  imagname,   &imagname_len },
        { sizeof(state),     JPI$_STATE,     &state,     nullptr },
        { sizeof(cputim),    JPI$_CPUTIM,    &cputim,    nullptr },
        { sizeof(pri),       JPI$_PRI,       &pri,       nullptr },
        { sizeof(ppgcnt),    JPI$_PPGCNT,    &ppgcnt,    nullptr },
        { sizeof(gpgcnt),    JPI$_GPGCNT,    &gpgcnt,    nullptr },
        { sizeof(virtpeak),  JPI$_VIRTPEAK,  &virtpeak,  nullptr },
        { sizeof(kt_count),  JPI$_KT_COUNT,  &kt_count,  nullptr },
        { sizeof(logintim),  JPI$_LOGINTIM,  &logintim,  nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;

    // Iterate through all processes
    while (true) {
        // Reset buffers
        pid = 0;
        owner_pid = 0;
        std::memset(prcnam, 0, sizeof(prcnam));
        prcnam_len = 0;
        std::memset(username, 0, sizeof(username));
        username_len = 0;
        std::memset(imagname, 0, sizeof(imagname));
        imagname_len = 0;
        state = 0;
        cputim = 0;
        pri = 0;
        ppgcnt = 0;
        gpgcnt = 0;
        virtpeak = 0;
        kt_count = 0;
        logintim = 0;

        status = sys$getjpiw(EFN$C_ENF, &scan_ctx, nullptr,
                             jpi_items, &iosb, nullptr, 0);

        if (iosb.iosb$l_getxxi_status == SS$_NOMOREPROC) {
            break;
        }

        if (!$VMS_STATUS_SUCCESS(status)) {
            // Some processes may not be accessible (privilege); skip them
            continue;
        }
        if (!$VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
            continue;
        }

        ProcessInfo info;
        info.pid = static_cast<int>(pid);
        info.parent_pid = static_cast<int>(owner_pid);
        info.name = trim_vms_string(prcnam, prcnam_len);
        info.user_name = trim_vms_string(username, username_len);
        info.state_char = map_state(state);
        info.priority = static_cast<int>(pri);

        // Image name serves as both command_line and executable_path on VMS.
        // VMS doesn't have a command-line arguments concept like Unix.
        info.executable_path = trim_vms_string(imagname, imagname_len);
        info.command_line = info.executable_path;
        if (info.command_line.empty()) {
            info.command_line = info.name;
        }

        // Memory: ppgcnt + gpgcnt = pages in working set, in pagelets
        info.resident_memory = static_cast<int64_t>(ppgcnt + gpgcnt) * VMS_PAGELET_SIZE;
        info.virtual_memory = static_cast<int64_t>(virtpeak) * VMS_PAGELET_SIZE;
        if (total_memory > 0) {
            info.memory_percent =
                (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
        }

        // CPU time: JPI$_CPUTIM returns accumulated CPU time in 10-millisecond ticks.
        // Store as user_time for delta calculation by DataStore.
        // VMS doesn't separate user/kernel CPU time per process.
        info.user_time = cputim;
        info.kernel_time = 0;

        // Thread count
        info.thread_count = static_cast<int>(kt_count);
        if (info.thread_count == 0) {
            info.thread_count = 1;  // Every process has at least one thread
        }

        // Start time: JPI$_LOGINTIM is 64-bit VMS absolute time
        info.start_time = vms_time_to_chrono(logintim);

        processes.push_back(std::move(info));
    }

    return processes;
}

std::optional<ProcessInfo> OpenVMSProcessDataProvider::get_process_info(int pid,
                                                                        int64_t total_memory) {
    if (pid <= 0) {
        return std::nullopt;
    }

    // Get total memory if not provided
    if (total_memory < 0) {
        unsigned int memsize = 0;
        ILE3 syi_items[] = {
            { sizeof(memsize), SYI$_MEMSIZE, &memsize, nullptr },
            { 0, 0, nullptr, nullptr }
        };
        IOSB syi_iosb;
        unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                          syi_items, &syi_iosb, nullptr, 0);
        if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(syi_iosb.iosb$l_getxxi_status)) {
            total_memory = static_cast<int64_t>(memsize) * VMS_PAGELET_SIZE;
        }
    }

    // Query a specific process by PID
    unsigned int vms_pid = static_cast<unsigned int>(pid);
    unsigned int owner_pid = 0;
    char prcnam[16] = {};
    unsigned short prcnam_len = 0;
    char username[13] = {};
    unsigned short username_len = 0;
    char imagname[256] = {};
    unsigned short imagname_len = 0;
    unsigned int state = 0;
    unsigned int cputim = 0;
    unsigned int pri = 0;
    unsigned int ppgcnt = 0;
    unsigned int gpgcnt = 0;
    unsigned int virtpeak = 0;
    unsigned int kt_count = 0;
    int64_t logintim = 0;

    ILE3 jpi_items[] = {
        { sizeof(owner_pid), JPI$_OWNER,     &owner_pid, nullptr },
        { sizeof(prcnam),    JPI$_PRCNAM,    prcnam,     &prcnam_len },
        { sizeof(username),  JPI$_USERNAME,  username,   &username_len },
        { sizeof(imagname),  JPI$_IMAGNAME,  imagname,   &imagname_len },
        { sizeof(state),     JPI$_STATE,     &state,     nullptr },
        { sizeof(cputim),    JPI$_CPUTIM,    &cputim,    nullptr },
        { sizeof(pri),       JPI$_PRI,       &pri,       nullptr },
        { sizeof(ppgcnt),    JPI$_PPGCNT,    &ppgcnt,    nullptr },
        { sizeof(gpgcnt),    JPI$_GPGCNT,    &gpgcnt,    nullptr },
        { sizeof(virtpeak),  JPI$_VIRTPEAK,  &virtpeak,  nullptr },
        { sizeof(kt_count),  JPI$_KT_COUNT,  &kt_count,  nullptr },
        { sizeof(logintim),  JPI$_LOGINTIM,  &logintim,  nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getjpiw(EFN$C_ENF, &vms_pid, nullptr,
                                      jpi_items, &iosb, nullptr, 0);

    if (!$VMS_STATUS_SUCCESS(status) || !$VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        return std::nullopt;
    }

    ProcessInfo info;
    info.pid = pid;
    info.parent_pid = static_cast<int>(owner_pid);
    info.name = trim_vms_string(prcnam, prcnam_len);
    info.user_name = trim_vms_string(username, username_len);
    info.state_char = map_state(state);
    info.priority = static_cast<int>(pri);
    info.executable_path = trim_vms_string(imagname, imagname_len);
    info.command_line = info.executable_path.empty() ? info.name : info.executable_path;
    info.resident_memory = static_cast<int64_t>(ppgcnt + gpgcnt) * VMS_PAGELET_SIZE;
    info.virtual_memory = static_cast<int64_t>(virtpeak) * VMS_PAGELET_SIZE;
    if (total_memory > 0) {
        info.memory_percent =
            (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
    }
    info.user_time = cputim;
    info.kernel_time = 0;
    info.thread_count = kt_count > 0 ? static_cast<int>(kt_count) : 1;
    info.start_time = vms_time_to_chrono(logintim);

    return info;
}

std::vector<ParseError> OpenVMSProcessDataProvider::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    return recent_errors_;
}

void OpenVMSProcessDataProvider::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

} // namespace pex
