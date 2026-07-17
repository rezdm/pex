// Dependency-free unit tests for the platform-neutral core (issue #56).
// Run directly (exit code != 0 on failure) or via ctest.

#include "../src/core/format_utils.hpp"
#include "../src/core/services/data_store.hpp"
#include "../src/core/services/history_store.hpp"
#include "../src/core/services/settings.hpp"
#include "../src/core/services/snapshot_diff.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

// Portable environment set (setenv is POSIX-only; _putenv_s on Windows).
void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void check(const bool ok, const char* expr, const char* file, const int line) {
    g_checks++;
    if (!ok) {
        g_failures++;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

std::string read_whole_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------

void test_format_bytes() {
    using pex::format_bytes;

    CHECK_EQ(format_bytes(0), "0B");
    CHECK_EQ(format_bytes(1023), "1023B");
    CHECK_EQ(format_bytes(1024), "1.00K");
    CHECK_EQ(format_bytes(10 * 1024), "10.0K");
    CHECK_EQ(format_bytes(100 * 1024), "100K");
    CHECK_EQ(format_bytes(1536, false), "1.50 KB");
    CHECK_EQ(format_bytes(int64_t{1} << 30), "1.00G");
    CHECK_EQ(format_bytes(int64_t{1} << 40, false), "1.00 TB");
    // Negative values must not underflow into unit scaling
    CHECK_EQ(format_bytes(-5), "-5B");
}

void test_settings_roundtrip(const std::string& tmpdir) {
    // Point Settings at tmpdir via the config-dir env var it reads per
    // platform (APPDATA on Windows, XDG_CONFIG_HOME elsewhere).
#ifdef _WIN32
    set_env("APPDATA", tmpdir.c_str());
#else
    set_env("XDG_CONFIG_HOME", tmpdir.c_str());
#endif

    {
        pex::Settings s;
        s.set_int("test.int", 42);
        s.set_int("test.negative", -7);
        s.set_bool("test.on", true);
        s.set_bool("test.off", false);
        CHECK(s.save());
    }
    {
        pex::Settings s;
        s.load();
        CHECK_EQ(s.get_int("test.int", 0), 42);
        CHECK_EQ(s.get_int("test.negative", 0), -7);
        CHECK_EQ(s.get_bool("test.on", false), true);
        CHECK_EQ(s.get_bool("test.off", true), false);
        // Missing keys fall back to defaults
        CHECK_EQ(s.get_int("test.missing", 123), 123);
        CHECK_EQ(s.get_bool("test.missing", true), true);
    }
}

// Build a single-process snapshot for HistoryStore tests
pex::DataSnapshot make_snapshot(const int pid, const std::string& name,
                                const double cpu_user, const double mem_pct) {
    pex::DataSnapshot snap;
    auto node = std::make_unique<pex::ProcessNode>();
    node->info.pid = pid;
    node->info.name = name;
    node->info.cpu_user_percent = cpu_user;
    node->info.cpu_kernel_percent = cpu_user / 2.0;
    node->info.memory_percent = mem_pct;
    node->info.resident_memory = 1024;
    snap.process_map[pid] = node.get();
    snap.process_tree.push_back(std::move(node));
    snap.cpu_usage = cpu_user;
    snap.memory_used = 512;
    snap.memory_total = 2048;
    snap.process_count = 1;
    snap.thread_count = 2;
    return snap;
}

void test_history_store() {
    pex::HistoryStore hs(3);  // Ring buffer of 3

    hs.record(make_snapshot(1, "proc-a", 10.0, 1.0));
    hs.record(make_snapshot(1, "proc-a", 20.0, 2.0));
    CHECK_EQ(hs.sample_count(), size_t{2});

    hs.record(make_snapshot(1, "proc-a", 30.0, 3.0));
    hs.record(make_snapshot(1, "proc-a", 40.0, 4.0));
    // Ring buffer bounded at 3; oldest sample dropped
    CHECK_EQ(hs.sample_count(), size_t{3});

    // Metric series: oldest -> newest, cpu = user + kernel
    const auto series = hs.get_metric_series(1, pex::HistoryMetric::Cpu, 10);
    CHECK_EQ(series.size(), size_t{3});
    if (series.size() == 3) {
        CHECK_EQ(series.front(), 30.0f);  // 20 + 10
        CHECK_EQ(series.back(), 60.0f);   // 40 + 20
    }
    // Absent PID yields zeros, not gaps
    const auto absent = hs.get_metric_series(999, pex::HistoryMetric::Cpu, 10);
    CHECK_EQ(absent.size(), size_t{3});
    if (!absent.empty()) CHECK_EQ(absent.back(), 0.0f);

    // Aggregation over the window
    const auto aggs = hs.aggregate(10);
    CHECK_EQ(aggs.size(), size_t{1});
    if (!aggs.empty()) {
        CHECK_EQ(aggs[0].pid, 1);
        CHECK_EQ(aggs[0].name, "proc-a");
        CHECK_EQ(aggs[0].present_ticks, size_t{3});
        CHECK_EQ(aggs[0].peak_cpu, 60.0f);
    }

    // Summed series for a PID set
    const auto ps = hs.get_series({1}, 10);
    CHECK_EQ(ps.cpu_user.size(), size_t{3});
    if (!ps.cpu_user.empty()) CHECK_EQ(ps.cpu_user.back(), 40.0f);
}

void test_csv_export_formula_injection(const std::string& tmpdir) {
    pex::HistoryStore hs(4);
    hs.record(make_snapshot(7, "=HYPERLINK(\"http://x\")", 5.0, 1.0));
    hs.record(make_snapshot(7, "=HYPERLINK(\"http://x\")", 5.0, 1.0));

    const std::string base = tmpdir + "/export-test";
    std::string error;
    CHECK(hs.export_csv(base, error));
    if (!error.empty()) std::fprintf(stderr, "export error: %s\n", error.c_str());

    const std::string proc_csv = read_whole_file(base + "-processes.csv");
    CHECK(!proc_csv.empty());
    // Name must be quoted, with inner quotes doubled and the leading '='
    // neutralized so spreadsheets do not evaluate it
    CHECK(proc_csv.find("\"'=HYPERLINK(\"\"http://x\"\")\"") != std::string::npos);
    CHECK(proc_csv.find(",=HYPERLINK") == std::string::npos);

    const std::string sys_csv = read_whole_file(base + "-system.csv");
    CHECK(sys_csv.find("cpu_usage_pct") != std::string::npos);
}

// Snapshot with an arbitrary set of PIDs (flat tree) for diff tests
std::shared_ptr<pex::DataSnapshot> make_pid_snapshot(const std::vector<int>& pids) {
    auto snap = std::make_shared<pex::DataSnapshot>();
    for (const int pid : pids) {
        auto node = std::make_unique<pex::ProcessNode>();
        node->info.pid = pid;
        node->info.name = "proc-" + std::to_string(pid);
        snap->process_map[pid] = node.get();
        snap->process_tree.push_back(std::move(node));
    }
    return snap;
}

void test_snapshot_diff() {
    const auto older = make_pid_snapshot({1, 2, 5});
    const auto newer = make_pid_snapshot({2, 3, 5, 7});

    const auto diff = pex::compute_snapshot_diff(older, newer.get());
    CHECK_EQ(diff.new_pids.size(), size_t{2});
    CHECK(diff.new_pids.contains(3));
    CHECK(diff.new_pids.contains(7));
    CHECK_EQ(diff.exited_processes.size(), size_t{1});
    if (!diff.exited_processes.empty()) {
        CHECK_EQ(diff.exited_processes[0]->pid, 1);
        CHECK_EQ(diff.exited_processes[0]->name, "proc-1");
    }

    // Identical snapshots: empty diff
    const auto same = pex::compute_snapshot_diff(newer, newer.get());
    CHECK(same.new_pids.empty());
    CHECK(same.exited_processes.empty());

    // No previous snapshot: the first tick must not flash everything green
    const auto first = pex::compute_snapshot_diff(nullptr, newer.get());
    CHECK(first.new_pids.empty());
    CHECK(first.exited_processes.empty());

    // Empty (but non-null) previous snapshot: the DataStore ctor publishes
    // one before the first scan; diffing against it must not flag everything
    // as new (launch green-flash guard).
    const auto empty_prev = make_pid_snapshot({});
    const auto vs_empty = pex::compute_snapshot_diff(empty_prev, newer.get());
    CHECK(vs_empty.new_pids.empty());
    CHECK(vs_empty.exited_processes.empty());

    // Exited ghosts are sorted by PID for deterministic rendering
    const auto empty_now = make_pid_snapshot({});
    const auto all_gone = pex::compute_snapshot_diff(newer, empty_now.get());
    CHECK_EQ(all_gone.exited_processes.size(), size_t{4});
    if (all_gone.exited_processes.size() == 4) {
        CHECK(all_gone.exited_processes[0]->pid < all_gone.exited_processes[1]->pid);
        CHECK(all_gone.exited_processes[2]->pid < all_gone.exited_processes[3]->pid);
    }

    // Exited-process pointers stay valid after the previous snapshot handle is
    // released, because the diff holds its own shared_ptr to it.
    {
        auto prev = make_pid_snapshot({100, 200});
        const auto now = make_pid_snapshot({200});
        auto lifetime_diff = pex::compute_snapshot_diff(prev, now.get());
        prev.reset();  // Drop the caller's handle; diff must keep it alive
        CHECK_EQ(lifetime_diff.exited_processes.size(), size_t{1});
        if (!lifetime_diff.exited_processes.empty()) {
            CHECK_EQ(lifetime_diff.exited_processes[0]->pid, 100);
        }
    }
}

} // namespace

int main() {
    // Private scratch area for files the tests create (portable: no mkdtemp,
    // which is POSIX-only and rooted at /tmp).
    namespace fs = std::filesystem;
    const auto unique =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string tmpdir =
        (fs::temp_directory_path() / ("pex-tests-" + std::to_string(unique))).string();
    std::error_code ec;
    fs::create_directories(tmpdir, ec);
    if (ec) {
        std::fprintf(stderr, "FATAL: cannot create temp dir %s\n", tmpdir.c_str());
        return 2;
    }

    test_format_bytes();
    test_settings_roundtrip(tmpdir);
    test_history_store();
    test_csv_export_formula_injection(tmpdir);
    test_snapshot_diff();

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
