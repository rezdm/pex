#include "windows_process_killer.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <format>
#include <map>
#include <vector>

namespace pex {

namespace {

uint64_t creation_time_of(const DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME create{}, exit_t{}, kernel{}, user{};
    uint64_t result = 0;
    if (GetProcessTimes(h, &create, &exit_t, &kernel, &user)) {
        ULARGE_INTEGER v;
        v.LowPart = create.dwLowDateTime;
        v.HighPart = create.dwHighDateTime;
        result = v.QuadPart;
    }
    CloseHandle(h);
    return result;
}

KillResult terminate_one(const DWORD pid) {
    KillResult result;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) {
        const DWORD err = GetLastError();
        result.success = false;
        result.process_still_running = (err != ERROR_INVALID_PARAMETER);  // Gone vs denied
        result.error_message = (err == ERROR_ACCESS_DENIED)
            ? "Access denied. Run elevated (Administrator) to terminate this process."
            : std::format("OpenProcess failed (error {})", err);
        return result;
    }
    if (TerminateProcess(h, 1)) {
        result.success = true;
    } else {
        result.success = false;
        result.process_still_running = true;
        result.error_message = std::format("TerminateProcess failed (error {})", GetLastError());
    }
    CloseHandle(h);
    return result;
}

} // namespace

std::optional<uint64_t> WindowsProcessKiller::process_start_token(const int pid) {
    if (pid <= 0) return std::nullopt;
    const uint64_t t = creation_time_of(static_cast<DWORD>(pid));
    if (t == 0) return std::nullopt;
    return t;
}

KillResult WindowsProcessKiller::kill_process(const int pid, bool /*force*/,
                                              std::optional<uint64_t> expected_token) {
    if (pid <= 4) {  // Idle/System are not killable
        return {false, true, "This system process cannot be terminated."};
    }
    if (auto refusal = check_kill_token(*this, pid, expected_token)) {
        return *refusal;
    }
    return terminate_one(static_cast<DWORD>(pid));
}

KillResult WindowsProcessKiller::kill_process_tree(const int pid, bool /*force*/,
                                                   std::optional<uint64_t> expected_token) {
    KillResult result;
    if (pid <= 4) {
        return {false, true, "This system process cannot be terminated."};
    }
    if (auto refusal = check_kill_token(*this, pid, expected_token)) {
        return *refusal;
    }
    const auto root = static_cast<DWORD>(pid);

    // Snapshot parent->children plus creation times for PID-reuse guarding:
    // a child is only genuine if it was created after its recorded parent.
    std::map<DWORD, std::vector<DWORD>> children;
    std::map<DWORD, uint64_t> created;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {false, true, "CreateToolhelp32Snapshot failed"};
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        children[entry.th32ParentProcessID].push_back(entry.th32ProcessID);
        created[entry.th32ProcessID] = creation_time_of(entry.th32ProcessID);
    }
    CloseHandle(snapshot);

    if (!created.contains(root) || created[root] == 0) {
        return {false, false, "Process not found. It may have already terminated."};
    }

    // Collect descendants (children first via post-order)
    std::vector<DWORD> order;
    std::vector<DWORD> stack{root};
    while (!stack.empty()) {
        const DWORD p = stack.back();
        stack.pop_back();
        order.push_back(p);
        if (const auto it = children.find(p); it != children.end()) {
            for (const DWORD child : it->second) {
                // PID-reuse guard: a real child was created after its parent
                if (child != p && created[child] >= created[p]) {
                    stack.push_back(child);
                }
            }
        }
    }

    int failed = 0;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {  // Leaves first
        // Re-check identity before terminating
        if (creation_time_of(*it) != created[*it]) continue;
        if (const KillResult r = terminate_one(*it); !r.success) failed++;
    }

    if (failed > 0) {
        result.success = false;
        result.process_still_running = true;
        result.error_message = std::format("Failed to terminate {} process(es) in the tree "
                                           "(may need Administrator).", failed);
    } else {
        result.success = true;
    }
    return result;
}

} // namespace pex
