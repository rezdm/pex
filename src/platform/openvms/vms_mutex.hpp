#pragma once

// VMS-compatible mutex replacement for std::mutex.
//
// On OpenVMS x86-64, libc++'s std::mutex default constructor relies on
// zero-initialization matching PTHREAD_MUTEX_INITIALIZER. This is true
// on Linux/FreeBSD but NOT on VMS, where PTHREAD_MUTEX_INITIALIZER
// contains non-zero fields. As a result, stack-allocated and
// heap-allocated std::mutex objects fail with EINVAL on lock().
//
// This wrapper uses explicit pthread_mutex_init()/destroy() to properly
// initialize mutexes in all storage classes (global, stack, heap).

#ifdef __VMS

#include <pthread.h>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <system_error>

namespace pex {

class vms_mutex {
public:
    vms_mutex() {
        int rc = pthread_mutex_init(&mtx_, nullptr);
        if (rc != 0) {
            throw std::system_error(rc, std::system_category(), "pthread_mutex_init failed");
        }
    }

    ~vms_mutex() {
        pthread_mutex_destroy(&mtx_);
    }

    // Non-copyable, non-movable
    vms_mutex(const vms_mutex&) = delete;
    vms_mutex& operator=(const vms_mutex&) = delete;

    void lock() {
        int rc = pthread_mutex_lock(&mtx_);
        if (rc != 0) {
            throw std::system_error(rc, std::system_category(), "mutex lock failed");
        }
    }

    void unlock() noexcept {
        pthread_mutex_unlock(&mtx_);
    }

    bool try_lock() noexcept {
        return pthread_mutex_trylock(&mtx_) == 0;
    }

    // For condition_variable interop
    pthread_mutex_t* native_handle() { return &mtx_; }

private:
    pthread_mutex_t mtx_;
};

// VMS-compatible condition_variable that works with vms_mutex
class vms_condition_variable {
public:
    vms_condition_variable() {
        int rc = pthread_cond_init(&cond_, nullptr);
        if (rc != 0) {
            throw std::system_error(rc, std::system_category(), "pthread_cond_init failed");
        }
    }

    ~vms_condition_variable() {
        pthread_cond_destroy(&cond_);
    }

    vms_condition_variable(const vms_condition_variable&) = delete;
    vms_condition_variable& operator=(const vms_condition_variable&) = delete;

    void notify_one() noexcept {
        pthread_cond_signal(&cond_);
    }

    void notify_all() noexcept {
        pthread_cond_broadcast(&cond_);
    }

    void wait(std::unique_lock<vms_mutex>& lock) {
        pthread_cond_wait(&cond_, lock.mutex()->native_handle());
    }

    template<typename Predicate>
    void wait(std::unique_lock<vms_mutex>& lock, Predicate pred) {
        while (!pred()) {
            wait(lock);
        }
    }

    template<typename Rep, typename Period>
    bool wait_for(std::unique_lock<vms_mutex>& lock,
                  const std::chrono::duration<Rep, Period>& rel_time) {
        auto abs_time = std::chrono::system_clock::now() + rel_time;
        return wait_until(lock, abs_time);
    }

    template<typename Rep, typename Period, typename Predicate>
    bool wait_for(std::unique_lock<vms_mutex>& lock,
                  const std::chrono::duration<Rep, Period>& rel_time,
                  Predicate pred) {
        auto deadline = std::chrono::system_clock::now() + rel_time;
        while (!pred()) {
            auto now = std::chrono::system_clock::now();
            if (now >= deadline) return pred();
            auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now);
            struct timespec ts;
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                deadline.time_since_epoch());
            ts.tv_sec = secs.count();
            ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline.time_since_epoch() - secs).count();
            pthread_cond_timedwait(&cond_, lock.mutex()->native_handle(), &ts);
        }
        return true;
    }

private:
    template<typename Clock, typename Duration>
    bool wait_until(std::unique_lock<vms_mutex>& lock,
                    const std::chrono::time_point<Clock, Duration>& abs_time) {
        auto secs = std::chrono::time_point_cast<std::chrono::seconds>(abs_time);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            abs_time - secs);
        struct timespec ts;
        ts.tv_sec = secs.time_since_epoch().count();
        ts.tv_nsec = ns.count();
        return pthread_cond_timedwait(&cond_, lock.mutex()->native_handle(), &ts) == 0;
    }

    pthread_cond_t cond_;
};

} // namespace pex

// Replace std::mutex and std::condition_variable for VMS builds
#define PEX_MUTEX pex::vms_mutex
#define PEX_CONDITION_VARIABLE pex::vms_condition_variable

#else

#include <mutex>
#include <condition_variable>

#define PEX_MUTEX std::mutex
#define PEX_CONDITION_VARIABLE std::condition_variable

#endif
