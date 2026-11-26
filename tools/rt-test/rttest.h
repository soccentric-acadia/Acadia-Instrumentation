// ============================================================================
//  Acadia Instrumentation
//
//  Copyright (C) 2026 SoCcentric
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  Licensed under the MIT License; see the LICENSE file in the repository root.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
// ============================================================================
/**
 * @file tools/rt-test/rttest.h
 * @brief core of the standalone `rt-test` PREEMPT_RT demonstration app
 *
 * (task 005). Not a daemon: a one-shot diagnostic that detects the kernel
 * scheduling model, runs the timing tests PREEMPT_RT improves (wakeup
 * latency under load, priority inversion with and without priority
 * inheritance, IPC wakeup latency, timer jitter, threaded-IRQ census), and
 * produces a machine-readable JSON report plus a human verdict line. It runs
 * on RT and non-RT kernels alike; on non-RT the same report shows what RT
 * would improve, which is the point of the demo.
 *
 * The timing-sensitive parts are deliberately thin wrappers around a set of
 * pure, unit-testable functions (identity parsing, distribution statistics,
 * report serialisation, verdict evaluation) so the repository gate never
 * depends on absolute latency values measured on a shared build host.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */

#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "instr/common/json.h"

namespace instr::rttest
{

/// @brief Suite options (defaults give a ~15 s end-to-end demo run).
struct Options
{
    unsigned duration_s = 10; ///< Wall time split across the wakeup-latency idle/load phases (seconds).
    unsigned threads = 0;     ///< Latency-loop thread count; 0 = one per online CPU.
    int prio = 80;            ///< Requested SCHED_FIFO priority (1..99; ignored when unprivileged).
    bool load = true;         ///< Run the under-load phase (synthetic CPU/memory/IO churn).
};

/// @brief Kernel scheduling-model identity, parsed from `uname` plus /sys.
struct KernelIdentity
{
    std::string release;          ///< uname(2) release, e.g. "6.6.78-rt51".
    std::string version;          ///< uname(2) version string, e.g. "#1 SMP PREEMPT_RT ...".
    bool preempt_rt = false;      ///< True when the version string carries PREEMPT_RT.
    bool realtime_sysfs = false;  ///< True when /sys/kernel/realtime reads "1" (RT-only file).
    std::string preemption_model; ///< Best-effort model name; "unknown" when not recognised.
};

/// @brief Parse the scheduling model out of uname strings (pure; unit-tested).
/// @param release uname release field.
/// @param version uname version string.
/// @param realtime_sysfs Whether /sys/kernel/realtime reported RT.
/// @return Parsed identity; preemption_model is "unknown" when unrecognised.
KernelIdentity identity_from_uname(const std::string& release, const std::string& version, bool realtime_sysfs);

/// @brief Read the identity from the running kernel (uname + /sys/kernel/realtime).
/// @return Parsed kernel identity.
KernelIdentity read_kernel_identity();

/// @brief Result of the RT scheduling probe (what the process may actually do).
struct SchedInfo
{
    bool fifo_granted = false;   ///< True when sched_setscheduler(SCHED_FIFO) succeeded.
    int requested_prio = 0;      ///< Priority the suite asked for.
    int granted_prio = 0;        ///< Priority actually in effect (== requested when granted).
    int rlimit_rtprio = -1;      ///< RLIMIT_RTPRIO soft limit; -1 when unreadable, -2 for unlimited.
    bool cap_sys_nice = false;   ///< True when the effective capability set has CAP_SYS_NICE (bit 21).
    int64_t rt_runtime_us = -1;  ///< /proc/sys/kernel/sched_rt_runtime_us; -1 when unreadable.
    int64_t rt_period_us = -1;   ///< /proc/sys/kernel/sched_rt_period_us; -1 when unreadable.
    std::string note;            ///< Degradation note when SCHED_FIFO was not granted.
};

/// @brief Probe RT scheduling: try SCHED_FIFO on the calling thread, read the RT rlimit and throttle knobs.
/// @param requested_prio Priority to request.
/// @return Probe result with the granted policy and every degradation note.
SchedInfo probe_sched(int requested_prio);

/// @brief Per-IRQ-line handler census: threaded handlers exist only under PREEMPT_RT.
struct IrqCensus
{
    long cpus = 0;        ///< Online CPU count (hardware concurrency).
    long irq_threads = 0; ///< Handler threads named "irq/..." visible in /proc (typically 0 on non-RT).
};

/// @brief Count online CPUs and `irq/...` handler threads (pure census of /proc; unit-tested indirectly).
/// @return The census; irq_threads is -1 when /proc could not be scanned.
IrqCensus census_irq_threads();

/// @brief Distribution summary over one measured latency sample set (values in microseconds).
struct Dist
{
    double min_us = 0;         ///< Smallest observed latency.
    double max_us = 0;         ///< Largest observed latency (the headline RT number).
    double avg_us = 0;         ///< Arithmetic mean.
    double p50_us = 0;         ///< 50th percentile.
    double p99_us = 0;         ///< 99th percentile.
    double p999_us = 0;        ///< 99.9th percentile.
    size_t samples = 0;        ///< Sample count.
    int bucket_us = 0;         ///< Histogram bucket width in microseconds.
    std::vector<int64_t> hist; ///< Histogram counts; the last bucket accumulates overflow.
};

/// @brief Reduce raw latency samples to a distribution summary (pure; unit-tested).
/// @param latencies_ns Sample set in nanoseconds (any order; consumed).
/// @param bucket_us Histogram bucket width in microseconds (> 0).
/// @return Summary with histogram (64 buckets, last one is overflow).
Dist summarize(std::vector<int64_t> latencies_ns, int bucket_us);

/// @brief Verdict of one suite run (pure; unit-tested).
struct Verdict
{
    bool preempt_rt = false;  ///< PREEMPT_RT detected on the running kernel.
    bool budget_met = false;  ///< Load-phase max latency within the RT budget.
    double budget_us = 500;   ///< Budget the verdict was measured against (task 002 D3: 500 µs).
    std::string summary;      ///< Human verdict line shown at the end of the table.
};

/// @brief Evaluate the verdict (pure; unit-tested).
/// @param preempt_rt Whether PREEMPT_RT was detected.
/// @param have_load_dist True when an under-load distribution exists.
/// @param load_max_us Max under-load wakeup latency (ignored when no distribution).
/// @param budget_us The RT latency budget (task 002 decision D3).
/// @return Verdict with a client-facing summary line.
Verdict evaluate_verdict(bool preempt_rt, bool have_load_dist, double load_max_us, double budget_us);

/// @brief One complete suite result: identity, probes, per-test metrics, verdict.
struct Report
{
    KernelIdentity kernel;          ///< Kernel scheduling-model identity.
    SchedInfo sched;                ///< RT scheduling probe result.
    IrqCensus irq;                  ///< Threaded-IRQ census.
    bool wakeup_done = false;       ///< Wakeup-latency test ran.
    Dist wakeup_idle;               ///< Wakeup latency, idle phase.
    Dist wakeup_load;               ///< Wakeup latency, under-load phase.
    bool inversion_done = false;    ///< Priority-inversion test ran.
    double non_pi_block_us = 0;     ///< Worst blocking without priority inheritance.
    double pi_block_us = 0;         ///< Worst blocking with PTHREAD_PRIO_INHERIT.
    bool inversion_capped = false;  ///< True when a scenario hit the safety cap (unbounded blocking).
    bool ipc_done = false;          ///< Pipe ping-pong test ran.
    Dist ipc;                       ///< Round-trip wakeup latency through a pipe.
    bool jitter_done = false;       ///< Timer-jitter test ran.
    double jitter_rate_hz = 0;      ///< Rate the jitter test ran at.
    Dist jitter;                    ///< Wakeup lateness at the high rate.
    Verdict verdict;                ///< Verdict evaluated after the tests.
    bool interrupted = false;       ///< True when SIGINT/SIGTERM ended the run early.
    std::vector<std::string> warnings; ///< Degradations and notes worth surfacing.
};

/// @brief One priority-inversion scenario (bounded): a low-prio holder blocks
/// a high-prio waiter while medium-prio spinners sit in between. With
/// PTHREAD_PRIO_INHERIT the holder is boosted and the block stays short;
/// without it the waiter is starved until the safety deadline (reported via
/// @p capped). Exposed so the scenario is drivable in isolation.
/// @param use_pi Attach PTHREAD_PRIO_INHERIT to the mutex when true.
/// @param prio_high Priority of the waiter (also the PI boost level).
/// @param stop_run Ends all spinning when set; also set internally on the cap.
/// @param capped Set true when the waiter hit the cap (unbounded blocking).
/// @return Blocking time in microseconds (the cap value when capped).
double run_inversion(bool use_pi, int prio_high, std::atomic<bool>& stop_run, bool& capped);

/// @brief Run the whole suite once; stops early when @p stop turns true.
/// @param opt Suite options.
/// @param stop Cooperative stop flag (SIGINT/SIGTERM).
/// @return The report; individual tests may be skipped (flagged via the done-flags).
Report run_suite(const Options& opt, std::atomic<bool>& stop);

/// @brief Serialise a report to JSON (pure; unit-tested for round-trip).
/// @param r Report to serialise.
/// @return JSON value tree (dump(2) produces the stored file content).
JValue report_json(const Report& r);

/// @brief UTC timestamp used for default result-file names ("YYYYMMDDTHHMMSSZ").
/// @return The timestamp string.
std::string utc_stamp();

} // namespace instr::rttest