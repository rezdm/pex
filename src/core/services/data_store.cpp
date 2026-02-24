#include "data_store.hpp"
#include <algorithm>
#ifdef __VMS
#else
#include <ranges>
#endif
#include <set>
#include <unordered_map>

namespace pex {

// Deep copy a ProcessNode tree
std::unique_ptr<ProcessNode> ProcessNode::clone() const {
    auto copy = std::make_unique<ProcessNode>();
    copy->info = info;
    copy->is_expanded = is_expanded;
    copy->tree_working_set = tree_working_set;
    copy->tree_memory_percent = tree_memory_percent;
    copy->tree_cpu_percent = tree_cpu_percent;
    copy->tree_total_cpu_percent = tree_total_cpu_percent;

    for (const auto& child : children) {
        copy->children.push_back(child->clone());
    }
    return copy;
}

DataStore::DataStore(IProcessDataProvider* process_provider, ISystemDataProvider* system_provider)
    : process_provider_(process_provider)
    , system_provider_(system_provider) {
    previous_system_cpu_times_ = system_provider_->get_cpu_times();
    previous_per_cpu_times_ = system_provider_->get_per_cpu_times();

    // Create initial empty snapshot
    current_snapshot_ = std::make_shared<DataSnapshot>();
    current_snapshot_->timestamp = std::chrono::steady_clock::now();
}

DataStore::~DataStore() {
    stop();
}

void DataStore::start() {
    if (running_) return;

    running_ = true;
    collection_thread_ = std::thread(&DataStore::collection_thread_func, this);
}

void DataStore::stop() {
    if (!running_) return;

    running_ = false;
    cv_.notify_all();

    if (collection_thread_.joinable()) {
#ifdef __VMS
        // On VMS, the collection thread may be blocked in sys$getjpiw
        // system service calls that don't respond to signals.
        // Wait up to 3 seconds, then detach — process exit will clean up.
        for (int i = 0; i < 30 && !thread_exited_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (thread_exited_.load()) {
            collection_thread_.join();
        } else {
            collection_thread_.detach();
        }
#else
        collection_thread_.join();
#endif
    }
}

void DataStore::set_refresh_interval(const int ms) {
    refresh_interval_ms_ = ms;
    cv_.notify_all(); // Wake up thread to adjust timing
}

int DataStore::get_refresh_interval() const {
    return refresh_interval_ms_;
}

std::shared_ptr<DataSnapshot> DataStore::get_snapshot() const {
    std::lock_guard lock(data_mutex_);
    return current_snapshot_;
}

void DataStore::refresh_now() {
    if (paused_) {
        refresh_pending_ = true;
        return;
    }
    force_refresh_ = true;
    cv_.notify_all();
}

void DataStore::pause() {
    paused_ = true;
}

void DataStore::resume() {
    paused_ = false;
    if (refresh_pending_.exchange(false)) {
        force_refresh_ = true;
    }
    cv_.notify_all();  // Wake up to resume collection
}

bool DataStore::is_paused() const {
    return paused_;
}

void DataStore::set_on_data_updated(std::function<void()> callback) {
    std::lock_guard lock(data_mutex_);
    on_data_updated_ = std::move(callback);
}

std::vector<ParseError> DataStore::get_recent_errors() const {
    return process_provider_->get_recent_errors();
}

void DataStore::collection_thread_func() {
    // Initial collection
    collect_data();

    while (running_) {
#ifdef __VMS
        // VMS: Polling approach avoids pthread_cond_timedwait issues.
        // On VMS, std::chrono::system_clock may use VMS epoch (1858) while
        // pthread_cond_timedwait expects POSIX epoch (1970), causing ACCVIO.
        // Instead, poll atomics every 50ms using std::this_thread::sleep_for.
        {
            int ms_left = refresh_interval_ms_.load();
            while (ms_left > 0 && running_.load() && !force_refresh_.load()) {
                int chunk = (ms_left < 50) ? ms_left : 50;
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                ms_left -= chunk;
            }
        }
#else
        std::unique_lock lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(refresh_interval_ms_), [this] {
            return !running_ || force_refresh_.load();
        });
#endif

        // Handle force_refresh_ - if paused, retain as pending
        if (force_refresh_.exchange(false) && paused_) {
            refresh_pending_ = true;
        }

        if (running_ && !paused_) {
            collect_data();
        }
    }
    thread_exited_ = true;
}

void DataStore::collect_data() {
    auto new_snapshot = std::make_shared<DataSnapshot>();
    new_snapshot->timestamp = std::chrono::steady_clock::now();

    // Get CPU times for delta calculation
    auto current_cpu_times = system_provider_->get_cpu_times();
    uint64_t total_cpu_delta = current_cpu_times.total() - previous_system_cpu_times_.total();

    // Read memory info once and reuse for processes and system stats
    const auto mem_info = system_provider_->get_memory_info();

    // Get all processes
    auto processes = process_provider_->get_all_processes(mem_info.total);

    // Calculate CPU percentages and collect current PIDs
    std::set<int> current_pids;
    unsigned int proc_count = system_provider_->get_processor_count();
    for (auto& proc : processes) {
        current_pids.insert(proc.pid);
        if (auto it = previous_cpu_times_.find(proc.pid); it != previous_cpu_times_.end()) {
            const auto [prev_user, prev_kernel] = it->second;
            const bool counters_valid = proc.user_time >= prev_user && proc.kernel_time >= prev_kernel;
            if (counters_valid && total_cpu_delta > 0) {
                const uint64_t user_delta = proc.user_time - prev_user;
                const uint64_t kernel_delta = proc.kernel_time - prev_kernel;
                const uint64_t process_delta = user_delta + kernel_delta;
                proc.cpu_percent = static_cast<double>(process_delta) / total_cpu_delta * 100.0 * proc_count;
                proc.total_cpu_percent = static_cast<double>(process_delta) / total_cpu_delta * 100.0;
            } else {
                // PID reused or counters wrapped – reset baseline
                proc.cpu_percent = 0.0;
                proc.total_cpu_percent = 0.0;
            }
        }
        previous_cpu_times_[proc.pid] = {proc.user_time, proc.kernel_time};
    }

    // Prune stale entries for processes that no longer exist
#ifdef __VMS
    // C++17 fallback: std::erase_if and set::contains() are C++20
    for (auto it = previous_cpu_times_.begin(); it != previous_cpu_times_.end(); ) {
        if (current_pids.find(it->first) == current_pids.end())
            it = previous_cpu_times_.erase(it);
        else
            ++it;
    }
#else
    std::erase_if(previous_cpu_times_, [&current_pids](const auto& entry) {
        return !current_pids.contains(entry.first);
    });
#endif

    // Build process tree
    std::unordered_map<int, std::unique_ptr<ProcessNode>> nodes;
    for (auto& proc : processes) {
        auto node = std::make_unique<ProcessNode>();
        node->info = std::move(proc);
        node->is_expanded = true;
        nodes[node->info.pid] = std::move(node);
    }

    // Find root nodes
    std::set<int> root_pids;
    for (auto& [pid, node] : nodes) {
        if (int ppid = node->info.parent_pid; ppid == pid || nodes.find(ppid) == nodes.end()) {
            root_pids.insert(pid);
        }
    }

    // Build children map
    std::unordered_map<int, std::vector<int>> children_map;
    for (auto& [pid, node] : nodes) {
        if (int ppid = node->info.parent_pid; ppid != pid && nodes.find(ppid) != nodes.end()) {
            children_map[ppid].push_back(pid);
        }
    }

    // Recursive function to attach children
    std::function<void(ProcessNode*, std::unordered_map<int, std::unique_ptr<ProcessNode>>&)> attach_children;
    attach_children = [&](ProcessNode* parent, std::unordered_map<int, std::unique_ptr<ProcessNode>>& all_nodes) {
        if (const auto it = children_map.find(parent->info.pid); it != children_map.end()) {
            for (int child_pid : it->second) {
                if (auto child_it = all_nodes.find(child_pid); child_it != all_nodes.end()) {
                    auto child = std::move(child_it->second);
                    attach_children(child.get(), all_nodes);
                    parent->children.push_back(std::move(child));
                }
            }
        }
    };

    // Build root nodes with their children
    for (int root_pid : root_pids) {
        if (auto it = nodes.find(root_pid); it != nodes.end()) {
            auto root = std::move(it->second);
            attach_children(root.get(), nodes);
            new_snapshot->process_tree.push_back(std::move(root));
        }
    }

    // Sort tree by PID
#ifdef __VMS
    std::sort(new_snapshot->process_tree.begin(), new_snapshot->process_tree.end(),
#else
    std::ranges::sort(new_snapshot->process_tree,
#endif
                      [](const auto& a, const auto& b) { return a->info.pid < b->info.pid; });

    // Build process map and calculate tree totals
    for (auto& root : new_snapshot->process_tree) {
        build_process_map(root.get(), new_snapshot->process_map);
        calculate_tree_totals(*root);
    }

    // Count threads and running processes
    new_snapshot->thread_count = 0;
    new_snapshot->running_count = 0;
    for (const auto& [_, node] : new_snapshot->process_map) {
        new_snapshot->thread_count += node->info.thread_count;
        if (node->info.state_char == 'R') {
            new_snapshot->running_count++;
        }
    }

    new_snapshot->memory_used = mem_info.used;
    new_snapshot->memory_total = mem_info.total;
    new_snapshot->process_count = static_cast<int>(processes.size());

    if (total_cpu_delta > 0) {
        uint64_t active_delta = current_cpu_times.active() - previous_system_cpu_times_.active();
        new_snapshot->cpu_usage = static_cast<double>(active_delta) / total_cpu_delta * 100.0;
    }

    // Per-CPU usage (reuse pre-allocated buffers)
    system_provider_->get_per_cpu_times(current_per_cpu_times_);
    const size_t cpu_count = current_per_cpu_times_.size();

    // Ensure buffers are sized correctly
    if (per_cpu_usage_buffer_.size() != cpu_count) {
        per_cpu_usage_buffer_.resize(cpu_count, 0.0);
        per_cpu_user_buffer_.resize(cpu_count, 0.0);
        per_cpu_system_buffer_.resize(cpu_count, 0.0);
    }

    if (cpu_count == previous_per_cpu_times_.size()) {
        for (size_t i = 0; i < cpu_count; i++) {
            if (uint64_t delta_total = current_per_cpu_times_[i].total() - previous_per_cpu_times_[i].total(); delta_total > 0) {
                const uint64_t delta_user = (current_per_cpu_times_[i].user + current_per_cpu_times_[i].nice) -
                                            (previous_per_cpu_times_[i].user + previous_per_cpu_times_[i].nice);
                const uint64_t delta_system = current_per_cpu_times_[i].system - previous_per_cpu_times_[i].system;
                const uint64_t delta_irq = current_per_cpu_times_[i].irq - previous_per_cpu_times_[i].irq;
                const uint64_t delta_softirq = current_per_cpu_times_[i].softirq - previous_per_cpu_times_[i].softirq;
                const uint64_t delta_active = current_per_cpu_times_[i].active() - previous_per_cpu_times_[i].active();
                const uint64_t delta_kernel = delta_system + delta_irq + delta_softirq;

                per_cpu_usage_buffer_[i] = static_cast<double>(delta_active) / delta_total * 100.0;
                per_cpu_user_buffer_[i] = static_cast<double>(delta_user) / delta_total * 100.0;
                per_cpu_system_buffer_[i] = static_cast<double>(delta_kernel) / delta_total * 100.0;
            } else {
                per_cpu_usage_buffer_[i] = 0.0;
                per_cpu_user_buffer_[i] = 0.0;
                per_cpu_system_buffer_[i] = 0.0;
            }
        }
    } else {
#ifdef __VMS
        std::fill(per_cpu_usage_buffer_.begin(), per_cpu_usage_buffer_.end(), 0.0);
        std::fill(per_cpu_user_buffer_.begin(), per_cpu_user_buffer_.end(), 0.0);
        std::fill(per_cpu_system_buffer_.begin(), per_cpu_system_buffer_.end(), 0.0);
#else
        std::ranges::fill(per_cpu_usage_buffer_, 0.0);
        std::ranges::fill(per_cpu_user_buffer_, 0.0);
        std::ranges::fill(per_cpu_system_buffer_, 0.0);
#endif
    }

    // Copy to snapshot (snapshot needs its own copy for thread safety)
    new_snapshot->per_cpu_usage = per_cpu_usage_buffer_;
    new_snapshot->per_cpu_user = per_cpu_user_buffer_;
    new_snapshot->per_cpu_system = per_cpu_system_buffer_;

    // Swap current to previous (reuses memory)
    std::swap(previous_per_cpu_times_, current_per_cpu_times_);

    // Additional system info
    new_snapshot->swap_info = system_provider_->get_swap_info();
    new_snapshot->load_average = system_provider_->get_load_average();
    new_snapshot->uptime_info = system_provider_->get_uptime();

    // Update previous values
    previous_system_cpu_times_ = current_cpu_times;

    // Atomically swap the snapshot
    std::function<void()> callback;
    {
        std::lock_guard lock(data_mutex_);
        current_snapshot_ = new_snapshot;
        callback = on_data_updated_;
    }

    // Notify callback outside of lock
    if (callback) {
        callback();
    }
}

void DataStore::calculate_tree_totals(ProcessNode& node) {
    node.tree_working_set = node.info.resident_memory;
    node.tree_memory_percent = node.info.memory_percent;
    node.tree_cpu_percent = node.info.cpu_percent;
    node.tree_total_cpu_percent = node.info.total_cpu_percent;

    for (auto& child : node.children) {
        calculate_tree_totals(*child);
        node.tree_working_set += child->tree_working_set;
        node.tree_memory_percent += child->tree_memory_percent;
        node.tree_cpu_percent += child->tree_cpu_percent;
        node.tree_total_cpu_percent += child->tree_total_cpu_percent;
    }
}

void DataStore::build_process_map(ProcessNode* node, std::unordered_map<int, ProcessNode*>& map) {
    map[node->info.pid] = node;
    for (auto& child : node->children) {
        build_process_map(child.get(), map);
    }
}

} // namespace pex
