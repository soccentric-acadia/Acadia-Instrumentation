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
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file tools/rt-test/main.cpp
 * @brief one-shot PREEMPT_RT demonstration app (task 005).
 *
 * Not a daemon: runs the suite once, prints a human-readable comparison
 * table, stores a JSON result file, and exits. Designed to be run by an
 * operator on a target (as root, so SCHED_FIFO is available) and by
 * developers on any host; on non-RT kernels the same report shows the
 * metrics PREEMPT_RT would improve.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "instr/common/json.h"
#include "rttest.h"

namespace
{
/// @brief Cooperative stop flag shared with the suite workers.
std::atomic<bool> g_stop{false};

/// @brief SIGINT/SIGTERM handler: request a cooperative stop (report is still written).
/// @param Unused signal number.
void on_signal(int)
{
    g_stop = true;
}

/// @brief Parsed command line.
struct Args
{
    std::string out;           ///< --out PATH (empty: ./rt-test-results-<stamp>.json; "-": stdout only).
    unsigned duration_s = 10;  ///< --duration SECONDS (per-phase measurement wall time).
    int prio = 80;             ///< --prio N (SCHED_FIFO priority 1..99).
    unsigned threads = 0;      ///< --threads N (0 = one per online CPU).
    bool load = true;          ///< --load on|off.
    bool json = false;         ///< --json: print the report JSON to stdout too.
    bool quiet = false;        ///< --quiet: skip the table (JSON/file only).
    bool want_help = false;    ///< --help.
};

/// @brief Print usage.
void usage()
{
    std::printf("instr-rt-test — PREEMPT_RT feature demonstration (Acadia Instrumentation)\n"
                "Not a daemon: runs once, prints a comparison table, stores a JSON result file, exits.\n"
                "usage: instr-rt-test [--out PATH|-] [--duration SECONDS] [--load on|off]\n"
                "                     [--prio N] [--threads N] [--json] [--quiet] [--help]\n"
                "\n"
                "  --out PATH       result file path; '-' writes to stdout only\n"
                "                   (default: ./rt-test-results-<UTC timestamp>.json)\n"
                "  --duration N     seconds per wakeup-latency phase, 1..300 (default 10)\n"
                "  --load on|off    include the under-load phase (default on)\n"
                "  --prio N         SCHED_FIFO priority 1..99 (default 80; needs privileges)\n"
                "  --threads N      latency-loop threads (default: one per online CPU)\n"
                "  --json           also print the JSON report to stdout\n"
                "  --quiet          skip the human-readable table\n"
                "  --help           show this help\n"
                "\n"
                "The suite runs on RT and non-RT kernels; on non-RT it reports the metrics\n"
                "PREEMPT_RT improves. SCHED_FIFO requires root or an RLIMIT_RTPRIO/ CAP_SYS_NICE;\n"
                "without it the suite degrades to SCHED_OTHER and says so.\n");
}

/// @brief Parse argv; unknown or malformed flags exit non-zero.
/// @param argc Argument count.
/// @param argv Argument vector.
/// @return Parsed arguments.
Args parse_args(int argc, char** argv)
{
    Args a;
    auto need_value = [&](int& i) -> std::string {
        if (i + 1 >= argc)
        {
            std::fprintf(stderr, "instr-rt-test: %s requires a value\n", argv[i]);
            std::exit(2);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help")
            a.want_help = true;
        else if (arg == "--out")
            a.out = need_value(i);
        else if (arg == "--duration")
            a.duration_s = unsigned(std::atoi(need_value(i).c_str()));
        else if (arg == "--prio")
            a.prio = std::atoi(need_value(i).c_str());
        else if (arg == "--threads")
            a.threads = unsigned(std::atoi(need_value(i).c_str()));
        else if (arg == "--load")
            a.load = need_value(i) != "off";
        else if (arg == "--json")
            a.json = true;
        else if (arg == "--quiet")
            a.quiet = true;
        else
        {
            std::fprintf(stderr, "instr-rt-test: unknown argument '%s'\n", argv[i]);
            usage();
            std::exit(2);
        }
    }
    a.duration_s = std::min(300u, std::max(1u, a.duration_s));
    a.prio = std::min(99, std::max(1, a.prio));
    return a;
}

/// @brief One table row: test, metric, formatted value.
struct Row
{
    const char* test;  ///< Test name column.
    const char* metric; ///< Metric column.
    std::string value; ///< Formatted value column.
};

/// @brief Format one distribution as "max / p99 / avg µs (N samples)".
/// @param d Distribution to render.
/// @return Human-readable summary string.
std::string fmt_dist(const instr::rttest::Dist& d)
{
    char buf[128];
    if (d.samples == 0) return "not measured";
    std::snprintf(buf, sizeof(buf), "%.1f / %.1f / %.1f us  (%zu samples)", d.max_us, d.p99_us, d.avg_us, d.samples);
    return buf;
}

/// @brief Print the human-readable comparison table.
/// @param r Report to render.
void print_table(const instr::rttest::Report& r)
{
    std::printf("\n");
    std::printf("=========================================================================\n");
    std::printf(" instr-rt-test — PREEMPT_RT feature demonstration\n");
    std::printf("=========================================================================\n");
    std::printf(" kernel        : %s (%s)\n", r.kernel.release.c_str(), r.kernel.version.c_str());
    std::printf(" model         : %s  (PREEMPT_RT: %s, /sys/kernel/realtime: %s)\n", //
                r.kernel.preemption_model.c_str(), r.kernel.preempt_rt ? "yes" : "no", r.kernel.realtime_sysfs ? "yes" : "no");
    std::printf(" scheduling    : %s", r.sched.fifo_granted ? "SCHED_FIFO granted" : "SCHED_OTHER only");
    if (r.sched.fifo_granted) std::printf(" (prio %d)", r.sched.granted_prio);
    if (r.sched.rlimit_rtprio == -2)
        std::printf(", rtprio rlimit: unlimited");
    else if (r.sched.rlimit_rtprio >= 0)
        std::printf(", rtprio rlimit: %d", r.sched.rlimit_rtprio);
    std::printf(", CAP_SYS_NICE: %s\n", r.sched.cap_sys_nice ? "yes" : "no");
    if (r.sched.rt_runtime_us >= 0 && r.sched.rt_period_us > 0) std::printf(" rt throttling : %lld/%lld us per period\n", (long long)r.sched.rt_runtime_us, (long long)r.sched.rt_period_us);
    std::printf(" cpus          : %ld   threaded-IRQ handler threads: %ld\n", r.irq.cpus, r.irq.irq_threads);
    std::printf("-------------------------------------------------------------------------\n");
    std::printf(" %-24s %-26s %s\n", "test", "metric", "value");
    if (r.wakeup_done && r.wakeup_idle.samples != 0) std::printf(" %-24s %-26s %s\n", "wakeup-latency (idle)", "max / p99 / avg", fmt_dist(r.wakeup_idle).c_str());
    if (r.wakeup_done && r.wakeup_load.samples != 0) std::printf(" %-24s %-26s %s\n", "wakeup-latency (load)", "max / p99 / avg", fmt_dist(r.wakeup_load).c_str());
    if (r.inversion_done)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.1f us / %.1f us%s", r.non_pi_block_us, r.pi_block_us, r.inversion_capped ? "  (capped: unbounded)" : "");
        std::printf(" %-24s %-26s %s\n", "prio-inversion", "non-PI block / PI block", buf);
    }
    if (r.ipc_done) std::printf(" %-24s %-26s %s\n", "wake-latency-ipc", "roundtrip max / p99 / avg", fmt_dist(r.ipc).c_str());
    if (r.jitter_done)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.0f Hz: %s", r.jitter_rate_hz, fmt_dist(r.jitter).c_str());
        std::printf(" %-24s %-26s %s\n", "timer-jitter", "lateness max / p99 / avg", buf);
    }
    std::printf("-------------------------------------------------------------------------\n");
    for (const std::string& w : r.warnings) std::printf(" warning      : %s\n", w.c_str());
    std::printf(" verdict      : %s\n", r.verdict.summary.c_str());
    std::printf("\n");
}
} // namespace

/// @brief Entry: parse flags, run the suite once, print, store, exit.
/// @param argc Argument count.
/// @param argv Argument vector.
/// @return 0 on a completed run, 130 when interrupted, 2 on usage errors.
int main(int argc, char** argv)
{
    const Args args = parse_args(argc, argv);
    if (args.want_help)
    {
        usage();
        return 0;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    instr::rttest::Options opt;
    opt.duration_s = args.duration_s;
    opt.prio = args.prio;
    opt.threads = args.threads;
    opt.load = args.load;

    instr::rttest::Report r = instr::rttest::run_suite(opt, g_stop);
    if (!args.quiet) print_table(r);
    const instr::JValue report = instr::rttest::report_json(r);

    const std::string path = args.out.empty() ? "rt-test-results-" + instr::rttest::utc_stamp() + ".json" : args.out;
    if (path == "-")
    {
        std::printf("%s\n", report.dump(2).c_str());
        return r.interrupted ? 130 : 0;
    }
    std::ofstream out(path);
    if (!out)
    {
        std::fprintf(stderr, "instr-rt-test: cannot write result file '%s'\n", path.c_str());
        if (args.json) std::printf("%s\n", report.dump(2).c_str());
        return 1;
    }
    out << report.dump(2) << "\n";
    out.close();
    if (args.json) std::printf("%s\n", report.dump(2).c_str());
    if (!args.quiet) std::printf(" result file  : %s%s\n", path.c_str(), r.interrupted ? "  (run was interrupted)" : "");
    return r.interrupted ? 130 : 0;
}