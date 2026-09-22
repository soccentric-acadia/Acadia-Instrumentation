// ============================================================================
//  Acadia Instrumentation
//
//  Copyright (C) 2026 Soccentric. All rights reserved.
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  This file is part of a proprietary product of Soccentric.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file tools/rt-test/rttest.cpp
 * @brief PREEMPT_RT feature demonstration suite (task 005).
 *
 * Every timing function degrades gracefully: without SCHED_FIFO permission
 * the same loops run under SCHED_OTHER and the report records the
 * degradation, because a degraded run on a non-RT kernel *is* the result the
 * client came to see. All statistics, identity parsing, report serialisation
 * and verdict evaluation are pure functions so the repository gate tests
 * them without measuring anything.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include "rttest.h"

#include "instr/common/version_gen.h"

#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace instr::rttest
{
namespace
{
/// @brief Histogram bucket count; the last bucket accumulates overflow.
constexpr size_t kHistBuckets = 64;

/// @brief Safety cap for the priority-inversion scenarios (the demo must stay bounded).
constexpr int64_t kInversionCapNs = 3000000000;

/// @brief Volatile sink that keeps the CPU-burn loop un-optimisable.
volatile uint64_t g_burn_sink = 0;

/// @brief Monotonic now in nanoseconds.
/// @return CLOCK_MONOTONIC reading in nanoseconds.
int64_t now_ns()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

/// @brief Absolute-deadline sleep until @p deadline_ns.
/// @param deadline_ns CLOCK_MONOTONIC deadline in nanoseconds.
/// @return 0 on timeout, EINTR when interrupted (both mean "wake up now").
int sleep_until(int64_t deadline_ns)
{
    timespec ts;
    ts.tv_sec = time_t(deadline_ns / 1000000000);
    ts.tv_nsec = long(deadline_ns % 1000000000);
    return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}

/// @brief Pin the calling thread to CPU @p cpu (best effort; never fatal).
/// @param cpu CPU index (clamped modulo the online count).
void pin_to_cpu(unsigned cpu)
{
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(size_t(cpu) % size_t(n), &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

/// @brief Elevate the calling thread to SCHED_FIFO @p prio when permitted.
/// @param prio Requested priority.
/// @param granted Set true when the kernel accepted the request.
/// @return True when the thread runs SCHED_FIFO afterwards.
bool thread_set_fifo(int prio, bool& granted)
{
    sched_param sp;
    std::memset(&sp, 0, sizeof(sp));
    sp.sched_priority = prio;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0)
    {
        granted = true;
        return true;
    }
    return false;
}

/// @brief Portable CPU-burn loop used by the synthetic-load generators.
/// @param stop Loop ends when the flag turns true.
void burn_cpu(const std::atomic<bool>& stop)
{
    uint32_t x = 0x9e3779b9u;
    while (!stop.load(std::memory_order_relaxed))
    {
        for (int i = 0; i < 4096; ++i) x = x * 1664525u + 1013904223u;
        g_burn_sink = x; // observable store keeps the loop from being elided
    }
}

/// @brief Busy-spin for a fixed duration (bounded inversion-scenario holder).
/// @param stop Cooperative stop.
/// @param ns Spin length in nanoseconds.
void spin_for(const std::atomic<bool>& stop, int64_t ns)
{
    const int64_t end = now_ns() + ns;
    while (!stop.load(std::memory_order_relaxed) && now_ns() < end)
    {
        for (int i = 0; i < 1024; ++i) __asm__ __volatile__("");
    }
}

/// @brief Read one small integer file under /proc or /sys.
/// @param path File to read.
/// @param out Value read.
/// @return True when the file existed and parsed.
bool read_long(const std::string& path, long& out)
{
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    long v = 0;
    const bool ok = std::fscanf(f, "%ld", &v) == 1;
    std::fclose(f);
    if (ok) out = v;
    return ok;
}

/// @brief True when the effective capability set has CAP_SYS_NICE (bit 21).
/// @return True when /proc/self/status grants the bit; false otherwise.
bool have_cap_sys_nice()
{
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    unsigned long long cap = 0;
    while (std::fgets(line, sizeof(line), f))
        if (std::sscanf(line, "CapEff: %llx", &cap) == 1)
        {
            found = true;
            break;
        }
    std::fclose(f);
    return found && (cap & (1ULL << 21)) != 0;
}

/// @brief Measure absolute-deadline wakeup latencies on @p nthreads workers.
///
/// One loop per thread at 1 kHz, SCHED_FIFO when permitted, per-CPU pinned.
/// Fallen-behind loops catch up instead of flooding: a missed slot is skipped
/// (equidistant scheduling, matching cyclictest's behaviour).
///
/// @param stop Cooperative stop.
/// @param seconds Measurement wall time.
/// @param nthreads Worker thread count.
/// @param prio Requested SCHED_FIFO priority.
/// @param want_fifo Whether the probe granted SCHED_FIFO.
/// @param out Merged sample set in nanoseconds.
/// @return Warning text when workers could not elevate (empty when fine).
std::string measure_wakeup(std::atomic<bool>& stop, double seconds, unsigned nthreads, int prio, bool want_fifo, std::vector<int64_t>& out)
{
    std::vector<std::vector<int64_t>> per(nthreads);
    std::vector<std::thread> workers;
    std::atomic<bool> fifo_ok{want_fifo};
    const int64_t deadline = now_ns() + int64_t(seconds * 1e9);
    for (unsigned i = 0; i < nthreads; ++i)
    {
        per[i].reserve(1024);
        workers.emplace_back([&, i] {
            bool granted = false;
            if (want_fifo) thread_set_fifo(prio, granted);
            if (want_fifo && !granted) fifo_ok = false;
            pin_to_cpu(i);
            constexpr int64_t kPeriodNs = 1000000; // 1 kHz like cyclictest -i1000
            int64_t next = now_ns() + kPeriodNs;
            while (!stop.load(std::memory_order_relaxed) && next < deadline)
            {
                sleep_until(next);
                const int64_t lat = now_ns() - next;
                if (lat >= 0) per[i].push_back(lat);
                next += kPeriodNs;
                const int64_t now = now_ns();
                if (next < now) next = now + kPeriodNs; // fallen behind: skip missed slots
            }
        });
    }
    for (auto& w : workers) w.join();
    size_t total = 0;
    for (const auto& p : per) total += p.size();
    out.reserve(total);
    for (const auto& p : per) out.insert(out.end(), p.begin(), p.end());
    if (want_fifo && !fifo_ok.load()) return "SCHED_FIFO was refused during the latency loop; samples measured under SCHED_OTHER";
    return {};
}

/// @brief Synthetic load: CPU burners (one per online CPU) plus memory and IO churn.
///
/// Each generator ends when either @p stop_load (phase over) or @p stop
/// (SIGINT/SIGTERM) turns true, so a Ctrl-C never leaves load behind.
///
/// @param stop_load Ends the generators when the phase is over.
/// @param stop Global stop (interrupt).
void start_stop_load(const std::atomic<bool>& stop_load, const std::atomic<bool>& stop)
{
    const auto stopping = [&] { return stop_load.load(std::memory_order_relaxed) || stop.load(std::memory_order_relaxed); };
    const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    std::vector<std::thread> burners;
    for (long i = 0; i < cpus; ++i) burners.emplace_back([&] { burn_cpu(stop_load); });
    // Memory churn: repeatedly grow, touch and shrink a buffer so page
    // allocation and the allocator contend with the measured loops.
    std::thread mem([&] {
        std::vector<char> buf;
        size_t size = 1u << 22;
        while (!stopping())
        {
            size = size < (64u << 20) ? size * 2 : (1u << 22);
            std::vector<char> next(size, 1);
            for (size_t i = 0; i < next.size(); i += 4096) next[i] = char(i);
            std::swap(buf, next);
        }
    });
    // IO churn: small append-write + fsync cycle in /tmp.
    std::thread io([&] {
        const char* path = "/tmp/rt-test-io-churn.bin";
        std::FILE* f = std::fopen(path, "wb");
        if (!f) return;
        char buf[4096];
        std::memset(buf, 0x5a, sizeof(buf));
        while (!stopping())
        {
            if (std::fwrite(buf, 1, sizeof(buf), f) != sizeof(buf)) break;
            if (std::fflush(f) != 0) break;
            const int fd = fileno(f);
            if (fd >= 0) fsync(fd);
        }
        std::fclose(f);
        std::remove(path);
    });
    mem.join();
    io.join();
    for (auto& b : burners) b.join();
}

} // namespace

// ---------------------------------------------------------------------------
// priority-inversion scenario (public: drivable in isolation, see header)
// ---------------------------------------------------------------------------

double run_inversion(bool use_pi, int prio_high, std::atomic<bool>& stop_run, bool& capped)
{
    constexpr int64_t kHolderSpinNs = 50000000; // 50 ms of holding under contention
    const int prio_low = std::max(1, prio_high / 2);
    const int prio_mid = std::max(prio_low + 1, prio_high - 1);
    const long cpus = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (use_pi) pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_t m;
    pthread_mutex_init(&m, &attr);
    pthread_mutexattr_destroy(&attr);

    std::atomic<bool> held{false};
    std::atomic<bool> done{false};
    std::atomic<bool> acquired{false};
    std::atomic<int64_t> t_lock{0};
    std::atomic<int64_t> t_acq{0};

    std::thread holder([&] {
        bool granted = false;
        thread_set_fifo(prio_low, granted);
        pin_to_cpu(unsigned(cpus - 1));
        while (!acquired.load() && !stop_run.load())
        {
            pthread_mutex_lock(&m);
            held = true;
            spin_for(stop_run, kHolderSpinNs);
            pthread_mutex_unlock(&m);
        }
        done = true;
    });
    std::vector<std::thread> mediums;
    for (long i = 0; i < cpus; ++i)
        mediums.emplace_back([&] {
            bool granted = false;
            thread_set_fifo(prio_mid, granted);
            pin_to_cpu(unsigned(i));
            const int64_t deadline = now_ns() + kInversionCapNs + kHolderSpinNs;
            while (!done.load() && !stop_run.load() && now_ns() < deadline) __asm__ __volatile__("");
        });
    std::thread waiter([&] {
        bool granted = false;
        thread_set_fifo(prio_high, granted);
        pin_to_cpu(0);
        while (!held.load()) // sleep-wait so the low-prio holder can reach the lock
        {
            if (stop_run.load()) return;
            sleep_until(now_ns() + 500000);
        }
        t_lock = now_ns();
        pthread_mutex_lock(&m);
        t_acq = now_ns();
        acquired = true;
        pthread_mutex_unlock(&m);
    });

    // Bounded wait: if the waiter has not been served by the cap, stop the
    // holder's cycling BEFORE joining the waiter, so the join is finite and
    // the scenario is reported as capped (unbounded blocking over the window).
    const int64_t give_up = now_ns() + kInversionCapNs;
    while (!acquired.load() && !stop_run.load() && now_ns() < give_up) sleep_until(now_ns() + 1000000);
    const bool ok = acquired.load();
    capped = !ok;
    if (!ok) stop_run = true;
    waiter.join();
    for (auto& med : mediums) med.join();
    stop_run = true;
    holder.join();
    pthread_mutex_destroy(&m);
    if (capped) return double(kInversionCapNs) / 1000.0;
    return double(t_acq.load() - t_lock.load()) / 1000.0;
}

// ---------------------------------------------------------------------------
// identity
// ---------------------------------------------------------------------------

KernelIdentity identity_from_uname(const std::string& release, const std::string& version, bool realtime_sysfs)
{
    KernelIdentity k;
    k.release = release;
    k.version = version;
    k.realtime_sysfs = realtime_sysfs;
    const bool in_version = version.find("PREEMPT_RT") != std::string::npos;
    k.preempt_rt = in_version || realtime_sysfs;
    if (in_version)
        k.preemption_model = "PREEMPT_RT";
    else if (version.find("PREEMPT_DYNAMIC") != std::string::npos)
        k.preemption_model = "PREEMPT_DYNAMIC";
    else if (version.find("PREEMPT_VOLUNTARY") != std::string::npos)
        k.preemption_model = "PREEMPT_VOLUNTARY";
    else if (version.find("PREEMPT_NONE") != std::string::npos)
        k.preemption_model = "PREEMPT_NONE";
    else if (version.find("PREEMPT") != std::string::npos)
        k.preemption_model = "PREEMPT";
    else
        k.preemption_model = "unknown";
    return k;
}

KernelIdentity read_kernel_identity()
{
    utsname u;
    const bool ok = uname(&u) == 0;
    const std::string release = ok ? u.release : "";
    const std::string version = ok ? u.version : "";
    long rt = 0;
    const bool rt_file = read_long("/sys/kernel/realtime", rt);
    return identity_from_uname(release, version, rt_file && rt == 1);
}

// ---------------------------------------------------------------------------
// scheduling probe
// ---------------------------------------------------------------------------

SchedInfo probe_sched(int requested_prio)
{
    SchedInfo s;
    s.requested_prio = requested_prio;
    rlimit rl;
    if (getrlimit(RLIMIT_RTPRIO, &rl) == 0) s.rlimit_rtprio = rl.rlim_cur == RLIM_INFINITY ? -2 : int(rl.rlim_cur);
    s.cap_sys_nice = have_cap_sys_nice();
    sched_param sp;
    std::memset(&sp, 0, sizeof(sp));
    sp.sched_priority = requested_prio;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0)
    {
        s.fifo_granted = true;
        sched_param got;
        std::memset(&got, 0, sizeof(got));
        s.granted_prio = (sched_getparam(0, &got) == 0 && got.sched_priority > 0) ? got.sched_priority : requested_prio;
        // Leave the process as we found it: the workers elevate themselves.
        sched_param off;
        std::memset(&off, 0, sizeof(off));
        sched_setscheduler(0, SCHED_OTHER, &off);
    }
    else { s.note = std::string("SCHED_FIFO not granted (") + std::strerror(errno) + "); latency loops run under SCHED_OTHER"; }
    long runtime = 0, period = 0;
    if (read_long("/proc/sys/kernel/sched_rt_runtime_us", runtime)) s.rt_runtime_us = runtime;
    if (read_long("/proc/sys/kernel/sched_rt_period_us", period)) s.rt_period_us = period;
    return s;
}

IrqCensus census_irq_threads()
{
    IrqCensus c;
    c.cpus = sysconf(_SC_NPROCESSORS_ONLN);
    c.irq_threads = 0;
    DIR* d = opendir("/proc");
    if (!d)
    {
        c.irq_threads = -1;
        return c;
    }
    dirent* e = nullptr;
    while ((e = readdir(d)) != nullptr)
    {
        const std::string name = e->d_name;
        if (name.empty() || name[0] < '0' || name[0] > '9') continue;
        const std::string comm_path = "/proc/" + name + "/comm";
        std::FILE* f = std::fopen(comm_path.c_str(), "r");
        if (!f) continue;
        char comm[64];
        if (std::fgets(comm, sizeof(comm), f))
        {
            // PREEMPT_RT moves IRQ handlers into threads named "irq/<n>-<device>".
            if (std::strncmp(comm, "irq/", 4) == 0) c.irq_threads++;
        }
        std::fclose(f);
    }
    closedir(d);
    return c;
}

// ---------------------------------------------------------------------------
// statistics
// ---------------------------------------------------------------------------

Dist summarize(std::vector<int64_t> latencies_ns, int bucket_us)
{
    Dist d;
    d.samples = latencies_ns.size();
    d.bucket_us = bucket_us;
    if (d.samples == 0) return d;
    std::sort(latencies_ns.begin(), latencies_ns.end());
    const auto at = [&](double p) {
        const size_t idx = size_t(std::ceil(p * double(d.samples))) - 1;
        return latencies_ns[std::min(idx, d.samples - 1)];
    };
    double sum = 0;
    for (int64_t v : latencies_ns) sum += double(v);
    d.min_us = double(latencies_ns.front()) / 1000.0;
    d.max_us = double(latencies_ns.back()) / 1000.0;
    d.avg_us = sum / double(d.samples) / 1000.0;
    d.p50_us = double(at(0.50)) / 1000.0;
    d.p99_us = double(at(0.99)) / 1000.0;
    d.p999_us = double(at(0.999)) / 1000.0;
    d.hist.assign(kHistBuckets, 0);
    for (int64_t v : latencies_ns)
    {
        size_t idx = size_t(double(v) / 1000.0 / double(bucket_us));
        if (idx >= kHistBuckets) idx = kHistBuckets - 1; // overflow bucket
        d.hist[idx]++;
    }
    return d;
}

// ---------------------------------------------------------------------------
// verdict
// ---------------------------------------------------------------------------

Verdict evaluate_verdict(bool preempt_rt, bool have_load_dist, double load_max_us, double budget_us)
{
    Verdict v;
    v.preempt_rt = preempt_rt;
    v.budget_us = budget_us;
    char buf[512];
    if (preempt_rt)
    {
        v.budget_met = have_load_dist && load_max_us <= budget_us;
        if (!have_load_dist)
            std::snprintf(buf, sizeof(buf),
                          "PREEMPT_RT kernel detected, but the under-load latency phase did not run; no budget "
                          "verdict available.");
        else if (v.budget_met)
            std::snprintf(buf, sizeof(buf),
                          "PREEMPT_RT kernel detected: under-load wakeup latency is bounded (max %.1f \xc2\xb5s vs "
                          "%.0f \xc2\xb5s budget). Priority inheritance keeps priority-inversion blocking bounded, "
                          "and IRQ handlers run as threads.",
                          load_max_us, budget_us);
        else
            std::snprintf(buf, sizeof(buf),
                          "PREEMPT_RT kernel detected: under-load wakeup latency (max %.1f \xc2\xb5s) exceeds the "
                          "%.0f \xc2\xb5s budget; inspect load and hardware before commissioning.",
                          load_max_us, budget_us);
    }
    else
    {
        v.budget_met = false;
        if (have_load_dist)
            std::snprintf(buf, sizeof(buf),
                          "Non-RT kernel: under-load wakeup latency (max %.1f \xc2\xb5s here) is unbounded by "
                          "design without PREEMPT_RT; boot a PREEMPT_RT image to compare.",
                          load_max_us);
        else
            std::snprintf(buf, sizeof(buf),
                          "Non-RT kernel: without PREEMPT_RT under-load wakeup latency is unbounded by design; boot "
                          "a PREEMPT_RT image to compare.");
    }
    v.summary = buf;
    return v;
}

// ---------------------------------------------------------------------------
// the suite
// ---------------------------------------------------------------------------

Report run_suite(const Options& opt, std::atomic<bool>& stop)
{
    Report r;
    r.kernel = read_kernel_identity();
    r.sched = probe_sched(opt.prio);
    if (!r.sched.fifo_granted) r.warnings.push_back(r.sched.note);
    r.irq = census_irq_threads();

    unsigned nthreads = opt.threads;
    if (nthreads == 0)
    {
        const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = unsigned(cpus > 0 ? cpus : 1);
    }
    const double half = double(opt.duration_s) / 2.0;

    // --- wakeup latency, idle ---
    std::vector<int64_t> idle_ns;
    const std::string w1 = measure_wakeup(stop, half, nthreads, opt.prio, r.sched.fifo_granted, idle_ns);
    if (!w1.empty()) r.warnings.push_back(w1);
    if (!idle_ns.empty())
    {
        r.wakeup_done = true;
        r.wakeup_idle = summarize(std::move(idle_ns), 10);
    }

    // --- wakeup latency, under synthetic load ---
    if (opt.load && !stop.load())
    {
        std::atomic<bool> stop_load{false};
        std::thread load([&] { start_stop_load(stop_load, stop); });
        sleep_until(now_ns() + 500000000); // let the load spin up before measuring
        std::vector<int64_t> load_ns;
        const std::string w2 = measure_wakeup(stop, half, nthreads, opt.prio, r.sched.fifo_granted, load_ns);
        if (!w2.empty()) r.warnings.push_back(w2);
        stop_load = true;
        load.join();
        if (!load_ns.empty())
        {
            r.wakeup_load = summarize(std::move(load_ns), 10);
            r.wakeup_done = true;
        }
    }

    // --- priority inversion, with and without priority inheritance ---
    if (!stop.load())
    {
        for (int use_pi = 0; use_pi <= 1 && !stop.load(); ++use_pi)
        {
            std::atomic<bool> stop_run{false};
            bool capped = false;
            const double block_us = run_inversion(use_pi != 0, opt.prio, stop_run, capped);
            if (use_pi == 0)
                r.non_pi_block_us = block_us;
            else
                r.pi_block_us = block_us;
            if (capped) r.inversion_capped = true;
        }
        r.inversion_done = true;
        if (r.inversion_capped)
            r.warnings.push_back("priority-inversion scenario hit its safety cap: blocking was unbounded over the "
                                 "observation window");
    }

    // --- pipe ping-pong wakeup latency ---
    if (!stop.load())
    {
        int ping[2] = {-1, -1}, pong[2] = {-1, -1};
        if (pipe(ping) == 0 && pipe(pong) == 0)
        {
            std::atomic<bool> pinger_done{false};
            std::vector<int64_t> round_ns;
            const int64_t deadline = now_ns() + 2000000000; // 2 s window
            std::thread ponger([&] {
                bool granted = false;
                thread_set_fifo(std::max(1, opt.prio - 1), granted);
                pin_to_cpu(1);
                char c = 0;
                while (!stop.load() && !pinger_done.load())
                {
                    if (::read(ping[0], &c, 1) != 1) break;
                    if (::write(pong[1], "y", 1) != 1) break;
                }
            });
            std::thread pinger([&] {
                bool granted = false;
                thread_set_fifo(opt.prio, granted);
                pin_to_cpu(0);
                char c = 0;
                constexpr size_t kMaxIters = 5000;
                for (size_t i = 0; i < kMaxIters && !stop.load(); ++i)
                {
                    const int64_t t0 = now_ns();
                    if (::write(ping[1], "x", 1) != 1) break;
                    if (::read(pong[0], &c, 1) != 1) break;
                    round_ns.push_back(now_ns() - t0);
                    if (now_ns() > deadline) break;
                }
                // Close the write end before signalling done: the ponger is
                // likely blocked in read() and only EOF (all writers gone)
                // releases it. Without this the join below can hang forever.
                pinger_done = true;
                close(ping[1]);
                ping[1] = -1;
            });
            pinger.join();
            ponger.join();
            if (ping[0] >= 0) close(ping[0]);
            if (ping[1] >= 0) close(ping[1]);
            close(pong[0]);
            close(pong[1]);
            if (!round_ns.empty())
            {
                r.ipc_done = true;
                r.ipc = summarize(std::move(round_ns), 20);
            }
        }
        else
            r.warnings.push_back("pipe ping-pong test skipped: pipe() failed");
    }

    // --- timer jitter at a high rate ---
    if (!stop.load())
    {
        std::vector<int64_t> late_ns;
        const int64_t deadline = now_ns() + 2000000000; // 2 s window
        std::thread jitter_worker([&] {
            bool granted = false;
            thread_set_fifo(opt.prio, granted);
            pin_to_cpu(0);
            constexpr int64_t kPeriodNs = 100000; // 10 kHz
            int64_t next = now_ns() + kPeriodNs;
            while (!stop.load() && next < deadline)
            {
                sleep_until(next);
                const int64_t lat = now_ns() - next;
                if (lat >= 0) late_ns.push_back(lat);
                next += kPeriodNs;
                const int64_t now = now_ns();
                if (next < now) next = now + kPeriodNs;
            }
        });
        jitter_worker.join();
        if (!late_ns.empty())
        {
            r.jitter_done = true;
            r.jitter_rate_hz = 10000.0;
            r.jitter = summarize(std::move(late_ns), 5);
        }
    }

    // --- verdict ---
    r.verdict = evaluate_verdict(r.kernel.preempt_rt, r.wakeup_done && r.wakeup_load.samples != 0, //
                                 r.wakeup_load.max_us, r.verdict.budget_us);
    r.interrupted = stop.load();
    return r;
}

// ---------------------------------------------------------------------------
// report serialisation
// ---------------------------------------------------------------------------

namespace
{
/// @brief Serialise one Dist (histogram included; overflow bucket last).
/// @param d Distribution to serialise.
/// @return JSON object for the distribution.
JValue dist_json(const Dist& d)
{
    JValue o = JValue::object();
    o.set("min_us", JValue(d.min_us));
    o.set("avg_us", JValue(d.avg_us));
    o.set("p50_us", JValue(d.p50_us));
    o.set("p99_us", JValue(d.p99_us));
    o.set("p999_us", JValue(d.p999_us));
    o.set("max_us", JValue(d.max_us));
    o.set("samples", JValue(int64_t(d.samples)));
    o.set("bucket_us", JValue(d.bucket_us));
    JValue h = JValue::array();
    for (int64_t v : d.hist) h.push(JValue(v));
    o.set("histogram", h);
    return o;
}
} // namespace

JValue report_json(const Report& r)
{
    JValue root = JValue::object();
    root.set("tool", JValue("instr-rt-test"));
    root.set("version", JValue(INSTR_VERSION));
    root.set("generated_at_utc", JValue(utc_stamp()));

    JValue kernel = JValue::object();
    kernel.set("release", JValue(r.kernel.release));
    kernel.set("version", JValue(r.kernel.version));
    kernel.set("preempt_rt", JValue(r.kernel.preempt_rt));
    kernel.set("realtime_sysfs", JValue(r.kernel.realtime_sysfs));
    kernel.set("preemption_model", JValue(r.kernel.preemption_model));
    root.set("kernel", kernel);

    JValue sched = JValue::object();
    sched.set("fifo_granted", JValue(r.sched.fifo_granted));
    sched.set("requested_prio", JValue(r.sched.requested_prio));
    sched.set("granted_prio", JValue(r.sched.granted_prio));
    sched.set("rlimit_rtprio", JValue(r.sched.rlimit_rtprio));
    sched.set("cap_sys_nice", JValue(r.sched.cap_sys_nice));
    sched.set("rt_runtime_us", JValue(r.sched.rt_runtime_us));
    sched.set("rt_period_us", JValue(r.sched.rt_period_us));
    if (!r.sched.note.empty()) sched.set("note", JValue(r.sched.note));
    root.set("sched", sched);

    JValue census = JValue::object();
    census.set("cpus", JValue(r.irq.cpus));
    census.set("irq_threads", JValue(r.irq.irq_threads));
    root.set("census", census);

    JValue tests = JValue::object();
    if (r.wakeup_done)
    {
        JValue w = JValue::object();
        if (r.wakeup_idle.samples != 0) w.set("idle", dist_json(r.wakeup_idle));
        if (r.wakeup_load.samples != 0) w.set("load", dist_json(r.wakeup_load));
        tests.set("wakeup_latency", w);
    }
    if (r.inversion_done)
    {
        JValue inv = JValue::object();
        inv.set("non_pi_block_us", JValue(r.non_pi_block_us));
        inv.set("pi_block_us", JValue(r.pi_block_us));
        inv.set("capped", JValue(r.inversion_capped));
        tests.set("priority_inversion", inv);
    }
    if (r.ipc_done) tests.set("wake_latency_ipc", dist_json(r.ipc));
    if (r.jitter_done)
    {
        JValue j = JValue::object();
        j.set("rate_hz", JValue(r.jitter_rate_hz));
        j.set("wakeup_lateness", dist_json(r.jitter));
        tests.set("timer_jitter", j);
    }
    root.set("tests", tests);

    JValue verdict = JValue::object();
    verdict.set("preempt_rt", JValue(r.verdict.preempt_rt));
    verdict.set("budget_max_latency_us", JValue(r.verdict.budget_us));
    verdict.set("budget_met", JValue(r.verdict.budget_met));
    verdict.set("summary", JValue(r.verdict.summary));
    root.set("verdict", verdict);

    // What RT is expected to improve: static annotations next to measured
    // metrics, so one run on either kernel explains the delta to look for.
    JValue improvements = JValue::array();
    const char* rows[][2] = {
        {"wakeup_latency.load.max_us", "bounded under PREEMPT_RT; unbounded (seconds-scale outliers) on stock kernels"},
        {"wakeup_latency.load.p999_us", "tail percentile drops by an order of magnitude under PREEMPT_RT"},
        {"priority_inversion.pi_block_us", "priority inheritance bounds blocking; non-PI blocking is load-dependent"},
        {"wake_latency_ipc.max_us", "IPC wake latency tails shrink under PREEMPT_RT"},
        {"timer_jitter.wakeup_lateness.max_us", "high-rate timer wake jitter shrinks under PREEMPT_RT"},
        {"census.irq_threads", "threaded IRQ handlers exist only under PREEMPT_RT"},
    };
    for (const auto& row : rows)
    {
        JValue item = JValue::object();
        item.set("metric", JValue(row[0]));
        item.set("note", JValue(row[1]));
        improvements.push(item);
    }
    root.set("rt_improves", improvements);

    JValue warnings = JValue::array();
    for (const std::string& w : r.warnings) warnings.push(JValue(w));
    root.set("warnings", warnings);
    root.set("interrupted", JValue(r.interrupted));
    return root;
}

std::string utc_stamp()
{
    char buf[32];
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tm tm_utc;
    gmtime_r(&ts.tv_sec, &tm_utc);
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm_utc);
    return buf;
}

} // namespace instr::rttest