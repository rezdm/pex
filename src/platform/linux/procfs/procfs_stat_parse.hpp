#pragma once

#include <cstdint>
#include <sstream>
#include <string>

// Pure, dependency-free parsing of the fields pex needs from a
// /proc/<tid>/stat (or /proc/<pid>/stat) line. Kept header-only and free of
// any Linux headers so it can be unit-tested from the platform-neutral test
// binary — procfs field-counting is fragile (the processor field was long
// misread by one position, issue #87), so it deserves a captured-fixture test.

namespace pex::procfs {

struct ThreadStatFields {
    char state = '?';
    int priority = 0;
    int processor = -1;
    bool ok = false;
};

// Field indices per proc(5): the line is "pid (comm) state ppid ...". comm
// (field 2) may contain spaces and ')' so we split at the LAST ')'. After that
// the tail begins at field 3 (state). We read through rss (field 24), then the
// processor is field 39 — i.e. 14 fields (25..38: rsslim..exit_signal) are
// skipped, NOT 15. Reading one field too far lands on rt_priority (40), which
// is 0 for the vast majority of (non-realtime) threads.
inline ThreadStatFields parse_thread_stat(const std::string& stat_line) {
    ThreadStatFields out;

    const std::string::size_type comm_end = stat_line.rfind(')');
    if (comm_end == std::string::npos || comm_end + 2 >= stat_line.size()) return out;

    std::istringstream iss(stat_line.substr(comm_end + 2));

    std::string state;
    int ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
    unsigned int flags = 0;
    uint64_t minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0, utime = 0, stime = 0;
    int64_t cutime = 0, cstime = 0, priority = 0, nice = 0, num_threads = 0, itrealvalue = 0,
            starttime = 0;
    uint64_t vsize = 0, rss = 0;
    uint64_t skip[14] = {};  // fields 25..38 (rsslim .. exit_signal)
    int processor = 0;

    iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
        >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
        >> cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >> starttime
        >> vsize >> rss;
    for (uint64_t& s : skip) iss >> s;
    iss >> processor;

    out.state = state.empty() ? '?' : state[0];
    out.priority = static_cast<int>(priority);
    out.processor = iss.fail() ? -1 : processor;
    out.ok = !state.empty();
    return out;
}

} // namespace pex::procfs
