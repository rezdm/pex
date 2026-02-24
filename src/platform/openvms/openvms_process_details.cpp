#include "openvms_process_data_provider.hpp"

#include <cstring>

// OpenVMS system headers
#include <starlet.h>
#include <lib$routines.h>
#include <descrip.h>
#include <jpidef.h>
#include <ssdef.h>
#include <stsdef.h>
#include <efndef.h>
#include <iosbdef.h>
#include <iledef.h>

namespace pex {

std::vector<ThreadInfo> OpenVMSProcessDataProvider::get_threads(int pid) {
    std::vector<ThreadInfo> threads;

    if (pid <= 0) {
        return threads;
    }

    // First, get the kernel thread count and process name for this process
    unsigned int vms_pid = static_cast<unsigned int>(pid);
    unsigned int kt_count = 0;
    char prcnam[16] = {};
    unsigned short prcnam_len = 0;

    ILE3 count_items[] = {
        { sizeof(kt_count), JPI$_KT_COUNT, &kt_count,  nullptr },
        { sizeof(prcnam),   JPI$_PRCNAM,   prcnam,      &prcnam_len },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getjpiw(EFN$C_ENF, &vms_pid, nullptr,
                                      count_items, &iosb, nullptr, 0);

    if (!$VMS_STATUS_SUCCESS(status) || !$VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        return threads;
    }

    std::string process_name = trim_vms_string(prcnam, prcnam_len);

    if (kt_count <= 1) {
        // Single-threaded process: return one thread entry
        ThreadInfo ti;
        ti.tid = 0;
        ti.name = process_name;
        ti.state = '?';
        ti.priority = 0;
        ti.processor = -1;

        // Get additional details for the main thread
        unsigned int thread_state = 0;
        unsigned int thread_pri = 0;
        unsigned int cpu_id = 0;

        ILE3 thread_items[] = {
            { sizeof(thread_state), JPI$_STATE,  &thread_state, nullptr },
            { sizeof(thread_pri),   JPI$_PRI,    &thread_pri,   nullptr },
            { sizeof(cpu_id),       JPI$_CPU_ID, &cpu_id,       nullptr },
            { 0, 0, nullptr, nullptr }
        };

        unsigned int tid_pid = static_cast<unsigned int>(pid);
        status = sys$getjpiw(EFN$C_ENF, &tid_pid, nullptr,
                             thread_items, &iosb, nullptr, 0);

        if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
            ti.state = map_state(thread_state);
            ti.priority = static_cast<int>(thread_pri);
            ti.processor = (cpu_id == static_cast<unsigned int>(-1)) ? -1
                           : static_cast<int>(cpu_id);
        }

        threads.push_back(std::move(ti));
        return threads;
    }

    // Multi-threaded process: iterate kernel threads using JPI$_THREAD_INDEX.
    // On OpenVMS, kernel threads within a process share the same PID but have
    // different thread indices (0-based). We use the JPI$_INITIAL_THREAD_PID
    // and iterate with GETJPI_CONTROL_FLAGS to enumerate threads.
    // For simplicity, we report basic info for each thread index.
    for (unsigned int ti_idx = 0; ti_idx < kt_count; ++ti_idx) {
        ThreadInfo ti;
        ti.tid = static_cast<int>(ti_idx);
        ti.name = process_name;
        ti.state = '?';
        ti.priority = 0;
        ti.processor = -1;

        // Attempt to query per-thread info. On VMS, specifying
        // JPI$_THREAD_INDEX in the item list selects a specific kernel thread.
        unsigned int thread_state = 0;
        unsigned int thread_pri = 0;
        unsigned int cpu_id = 0;
        unsigned int thread_idx = ti_idx;

        ILE3 thread_items[] = {
            { sizeof(thread_idx),   JPI$_THREAD_INDEX, &thread_idx,   nullptr },
            { sizeof(thread_state), JPI$_STATE,        &thread_state, nullptr },
            { sizeof(thread_pri),   JPI$_PRI,          &thread_pri,   nullptr },
            { sizeof(cpu_id),       JPI$_CPU_ID,       &cpu_id,       nullptr },
            { 0, 0, nullptr, nullptr }
        };

        unsigned int tid_pid = static_cast<unsigned int>(pid);
        status = sys$getjpiw(EFN$C_ENF, &tid_pid, nullptr,
                             thread_items, &iosb, nullptr, 0);

        if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
            ti.state = map_state(thread_state);
            ti.priority = static_cast<int>(thread_pri);
            ti.processor = (cpu_id == static_cast<unsigned int>(-1)) ? -1
                           : static_cast<int>(cpu_id);
        }

        threads.push_back(std::move(ti));
    }

    return threads;
}

std::string OpenVMSProcessDataProvider::get_thread_stack(
    [[maybe_unused]] int pid, [[maybe_unused]] int tid) {
    // Thread stack traces are not available through standard OpenVMS system
    // services. Would require SDA (System Dump Analyzer) or DEBUG access,
    // which is not practical for a monitoring tool.
    return "";
}

std::vector<FileHandleInfo> OpenVMSProcessDataProvider::get_file_handles(
    [[maybe_unused]] int pid) {
    // OpenVMS does not provide a standard API to enumerate open files/channels
    // for another process. The channel table is a per-process kernel structure.
    // Enumerating it would require CMKRNL privilege and direct kernel memory access.
    //
    // JPI$_FILCNT returns the remaining open file quota (not the list of open files).
    // A future enhancement could parse the output of SHOW PROCESS/CHANNELS.
    return {};
}

std::vector<NetworkConnectionInfo> OpenVMSProcessDataProvider::get_network_connections(
    [[maybe_unused]] int pid) {
    // OpenVMS TCP/IP Services (TCPIP$) does not provide a per-process network
    // connection enumeration API. Network connections are associated with
    // BG (pseudo-device) channels assigned by the process.
    //
    // A future enhancement could:
    // 1. Use TCPIP$SHOW DEVICE to list all connections and correlate with PIDs
    // 2. Use the Management API (TCPIP$MANAGEMENT) if available
    // 3. Parse NETSTAT output
    return {};
}

std::vector<MemoryMapInfo> OpenVMSProcessDataProvider::get_memory_maps(
    [[maybe_unused]] int pid) {
    // OpenVMS does not have a /proc/PID/maps equivalent. The virtual address
    // space layout is managed by the image activator and is not exposed through
    // standard system services.
    //
    // JPI$_FREP0VA, JPI$_FREP1VA give free address boundaries, and working set
    // info is available via JPI$_WSSIZE/PPGCNT/GPGCNT, but a detailed per-region
    // map would require SDA or ANALYZE/PROCESS_DUMP.
    return {};
}

std::vector<EnvironmentVariable> OpenVMSProcessDataProvider::get_environment_variables(
    [[maybe_unused]] int pid) {
    // OpenVMS does not have Unix-style environment variables in the process
    // address space. The closest equivalents are:
    //
    // 1. DCL logical names (process, job, group, system tables)
    // 2. DCL symbols (local and global)
    //
    // These are managed by the CLI (Command Language Interpreter) and are not
    // directly accessible from another process via system services.
    //
    // A future enhancement could use $TRNLNM to translate logical names from
    // the process's logical name table, but this requires knowing which names
    // to look up.
    return {};
}

std::vector<LibraryInfo> OpenVMSProcessDataProvider::get_libraries(
    [[maybe_unused]] int pid) {
    // OpenVMS shared images (the equivalent of shared libraries) are activated
    // by the image activator when a process starts. The list of activated images
    // is maintained in the process's Image Activator context.
    //
    // JPI$_IMAGNAME returns only the currently executing image name, not the
    // list of all loaded shared images.
    //
    // A future enhancement could use ANALYZE/IMAGE on the executable to find
    // its shared image dependencies, or parse SHOW PROCESS/CONTINUOUS output.
    return {};
}

} // namespace pex
