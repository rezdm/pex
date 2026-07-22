#include "history_store.hpp"
#include "data_store.hpp"

#include <algorithm>
#include <format>
#include <fstream>

namespace pex {

HistoryStore::HistoryStore(const size_t max_samples)
    : max_samples_(max_samples == 0 ? 1 : max_samples) {}

void HistoryStore::record(const DataSnapshot& snapshot) {
    HistorySample sample;
    sample.wall_time = std::chrono::system_clock::now();
    sample.cpu_usage = static_cast<float>(snapshot.cpu_usage);
    sample.memory_used = snapshot.memory_used;
    sample.memory_total = snapshot.memory_total;
    sample.process_count = snapshot.process_count;
    sample.thread_count = snapshot.thread_count;

    sample.per_cpu_user.reserve(snapshot.per_cpu_user.size());
    for (double v : snapshot.per_cpu_user) sample.per_cpu_user.push_back(static_cast<float>(v));
    sample.per_cpu_system.reserve(snapshot.per_cpu_system.size());
    for (double v : snapshot.per_cpu_system) sample.per_cpu_system.push_back(static_cast<float>(v));

    sample.processes.reserve(snapshot.process_map.size());

    std::lock_guard lock(mutex_);
    for (const auto& [pid, node] : snapshot.process_map) {
        const ProcessInfo& info = node->info;
        ProcessSample ps;
        ps.pid = pid;
        ps.cpu_user_percent = static_cast<float>(info.cpu_user_percent);
        ps.cpu_kernel_percent = static_cast<float>(info.cpu_kernel_percent);
        ps.memory_percent = static_cast<float>(info.memory_percent);
        ps.resident_memory = info.resident_memory;
        ps.io_read_rate = static_cast<float>(info.io_read_rate);
        ps.io_write_rate = static_cast<float>(info.io_write_rate);
        sample.processes.push_back(ps);

        process_names_[pid] = info.name;
    }

    samples_.push_back(std::move(sample));
    while (samples_.size() > max_samples_) {
        samples_.pop_front();
    }

    // Bound the name map: drop names for PIDs no longer present in any
    // retained sample (cheap approximation: prune when it grows large).
    if (process_names_.size() > 4 * samples_.back().processes.size() + 1024) {
        std::unordered_map<int, std::string> kept;
        for (const auto& s : samples_) {
            for (const auto& ps : s.processes) {
                if (auto it = process_names_.find(ps.pid); it != process_names_.end()) {
                    kept[ps.pid] = it->second;
                }
            }
        }
        process_names_ = std::move(kept);
    }
}

size_t HistoryStore::sample_count() const {
    std::lock_guard lock(mutex_);
    return samples_.size();
}

size_t HistoryStore::count_since(const std::chrono::system_clock::time_point oldest) const {
    std::lock_guard lock(mutex_);
    // samples_ runs oldest -> newest; count the contiguous newest run in-window.
    size_t n = 0;
    for (auto it = samples_.rbegin(); it != samples_.rend(); ++it) {
        if (it->wall_time < oldest) break;
        ++n;
    }
    return n;
}

ProcessHistorySeries HistoryStore::get_series(const std::vector<int>& pids,
                                              const size_t max_points) const {
    ProcessHistorySeries series;

    std::lock_guard lock(mutex_);
    const size_t count = std::min(samples_.size(), max_points);
    if (count == 0) return series;

    series.cpu_user.reserve(count);
    series.cpu_kernel.reserve(count);
    series.memory_percent.reserve(count);

    const size_t first = samples_.size() - count;
    const size_t cpu_count = samples_.back().per_cpu_user.size();
    series.per_cpu_user.assign(cpu_count, {});
    series.per_cpu_kernel.assign(cpu_count, {});
    for (auto& v : series.per_cpu_user) v.reserve(count);
    for (auto& v : series.per_cpu_kernel) v.reserve(count);

    // O(1) membership instead of a linear scan of `pids` per process per sample.
    const std::unordered_set<int> pid_set(pids.begin(), pids.end());

    for (size_t i = first; i < samples_.size(); i++) {
        const HistorySample& s = samples_[i];

        float user = 0.0f, kernel = 0.0f, mem = 0.0f;
        for (const ProcessSample& ps : s.processes) {
            if (pid_set.contains(ps.pid)) {
                user += ps.cpu_user_percent;
                kernel += ps.cpu_kernel_percent;
                mem += ps.memory_percent;
            }
        }
        series.cpu_user.push_back(user);
        series.cpu_kernel.push_back(kernel);
        series.memory_percent.push_back(mem);

        for (size_t c = 0; c < cpu_count; c++) {
            series.per_cpu_user[c].push_back(c < s.per_cpu_user.size() ? s.per_cpu_user[c] : 0.0f);
            series.per_cpu_kernel[c].push_back(c < s.per_cpu_system.size() ? s.per_cpu_system[c] : 0.0f);
        }
    }

    return series;
}

std::vector<ProcessAggregate> HistoryStore::aggregate(const size_t max_samples) const {
    std::lock_guard lock(mutex_);

    const size_t count = std::min(samples_.size(), max_samples);
    if (count == 0) return {};
    const size_t first = samples_.size() - count;

    std::unordered_map<int, ProcessAggregate> by_pid;

    for (size_t i = first; i < samples_.size(); i++) {
        for (const ProcessSample& ps : samples_[i].processes) {
            ProcessAggregate& agg = by_pid[ps.pid];
            agg.pid = ps.pid;
            agg.present_ticks++;

            const float cpu = ps.cpu_user_percent + ps.cpu_kernel_percent;
            agg.avg_cpu += cpu;  // Sums for now; divided below
            agg.peak_cpu = std::max(agg.peak_cpu, cpu);
            agg.avg_mem += ps.memory_percent;
            agg.peak_mem = std::max(agg.peak_mem, ps.memory_percent);
            agg.avg_io_read += ps.io_read_rate;
            agg.peak_io_read = std::max(agg.peak_io_read, ps.io_read_rate);
            agg.avg_io_write += ps.io_write_rate;
            agg.peak_io_write = std::max(agg.peak_io_write, ps.io_write_rate);
        }
    }

    std::vector<ProcessAggregate> result;
    result.reserve(by_pid.size());
    const auto window = static_cast<float>(count);
    for (auto& [pid, agg] : by_pid) {
        agg.avg_cpu /= window;
        agg.avg_mem /= window;
        agg.avg_io_read /= window;
        agg.avg_io_write /= window;
        if (const auto it = process_names_.find(pid); it != process_names_.end()) {
            agg.name = it->second;
        }
        result.push_back(std::move(agg));
    }
    return result;
}

std::vector<float> HistoryStore::get_metric_series(const int pid, const HistoryMetric metric,
                                                   const size_t max_points) const {
    std::lock_guard lock(mutex_);

    const size_t count = std::min(samples_.size(), max_points);
    std::vector<float> series;
    series.reserve(count);
    if (count == 0) return series;
    const size_t first = samples_.size() - count;

    for (size_t i = first; i < samples_.size(); i++) {
        float value = 0.0f;
        for (const ProcessSample& ps : samples_[i].processes) {
            if (ps.pid != pid) continue;
            switch (metric) {
                case HistoryMetric::Cpu:     value = ps.cpu_user_percent + ps.cpu_kernel_percent; break;
                case HistoryMetric::Memory:  value = ps.memory_percent; break;
                case HistoryMetric::IoRead:  value = ps.io_read_rate; break;
                case HistoryMetric::IoWrite: value = ps.io_write_rate; break;
            }
            break;
        }
        series.push_back(value);
    }
    return series;
}

static std::string format_wall_time(const std::chrono::system_clock::time_point tp) {
    const auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val{};
#ifdef _WIN32
    localtime_s(&tm_val, &time);
#else
    localtime_r(&time, &tm_val);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count() % 1000;
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
                       tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                       tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, ms);
}

bool HistoryStore::export_csv(const std::string& base_path, std::string& error) const {
    const std::string system_path = base_path + "-system.csv";
    const std::string process_path = base_path + "-processes.csv";

    std::ofstream sys_file(system_path);
    std::ofstream proc_file(process_path);
    if (!sys_file || !proc_file) {
        error = "Cannot create " + (!sys_file ? system_path : process_path);
        return false;
    }

    sys_file << "time,cpu_usage_pct,memory_used_bytes,memory_total_bytes,process_count,thread_count\n";
    proc_file << "time,pid,name,cpu_user_pct,cpu_kernel_pct,mem_pct,rss_bytes,io_read_bps,io_write_bps\n";

    // Snapshot under the lock, then write the (potentially large) files without
    // holding it, so a slow export doesn't block the collector's next record()
    // (issue #86).
    std::deque<HistorySample> samples;
    std::unordered_map<int, std::string> names;
    {
        std::lock_guard lock(mutex_);
        samples = samples_;
        names = process_names_;
    }

    for (const HistorySample& s : samples) {
        const std::string ts = format_wall_time(s.wall_time);

        sys_file << ts << ',' << s.cpu_usage << ',' << s.memory_used << ','
                 << s.memory_total << ',' << s.process_count << ',' << s.thread_count << '\n';

        for (const ProcessSample& ps : s.processes) {
            std::string name;
            if (const auto it = names.find(ps.pid); it != names.end()) {
                name = it->second;
            }
            // CSV-quote the name (it may contain commas/quotes). Process
            // names are attacker-controlled; neutralize leading formula
            // characters so spreadsheets don't evaluate them (CSV injection).
            std::string quoted = "\"";
            if (!name.empty() && (name[0] == '=' || name[0] == '+' ||
                                  name[0] == '-' || name[0] == '@' ||
                                  name[0] == '\t' || name[0] == '\r')) {
                quoted += '\'';
            }
            for (char ch : name) {
                if (ch == '"') quoted += '"';
                quoted += ch;
            }
            quoted += '"';

            proc_file << ts << ',' << ps.pid << ',' << quoted << ','
                      << ps.cpu_user_percent << ',' << ps.cpu_kernel_percent << ','
                      << ps.memory_percent << ',' << ps.resident_memory << ','
                      << ps.io_read_rate << ',' << ps.io_write_rate << '\n';
        }
    }

    if (!sys_file.good() || !proc_file.good()) {
        error = "Write failed (disk full?)";
        return false;
    }
    return true;
}

} // namespace pex
