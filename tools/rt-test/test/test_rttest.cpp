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
 * @file tools/rt-test/test/test_rttest.cpp
 * @brief unit tests for the rt-test core (task 005).
 *
 * Only pure logic is tested: identity parsing, latency statistics, report
 * serialisation and verdict evaluation. Timing loops are deliberately NOT
 * asserted on measured values — a shared CI runner cannot make meaningful
 * latency claims, and the suite degrades by design when unprivileged.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include <gtest/gtest.h>

#include "rttest.h"

using namespace instr;
using namespace instr::rttest;

// ---------------------------------------------------------------------------
// kernel identity parsing
// ---------------------------------------------------------------------------

TEST(RtTestIdentity, ParsesRtKernel)
{
    const KernelIdentity k = identity_from_uname("6.6.78-rt51", "#1 SMP PREEMPT_RT Fri Feb 28 14:24:54 UTC 2025", true);
    EXPECT_TRUE(k.preempt_rt);
    EXPECT_TRUE(k.realtime_sysfs);
    EXPECT_EQ(k.preemption_model, "PREEMPT_RT");
    EXPECT_EQ(k.release, "6.6.78-rt51");
}

TEST(RtTestIdentity, RealtimeSysfsAloneMarksRt)
{
    // Some builds do not carry the string but do expose /sys/kernel/realtime.
    const KernelIdentity k = identity_from_uname("6.6.78", "#1 SMP ...", true);
    EXPECT_TRUE(k.preempt_rt);
}

TEST(RtTestIdentity, ParsesVoluntaryKernel)
{
    const KernelIdentity k = identity_from_uname("6.6.78", "#1 SMP PREEMPT_VOLUNTARY Fri Feb 28 14:24:54 UTC 2025", false);
    EXPECT_FALSE(k.preempt_rt);
    EXPECT_FALSE(k.realtime_sysfs);
    EXPECT_EQ(k.preemption_model, "PREEMPT_VOLUNTARY");
}

TEST(RtTestIdentity, ParsesFullPreemptKernel)
{
    const KernelIdentity k = identity_from_uname("6.6.78", "#1 SMP PREEMPT Fri Feb 28 14:24:54 UTC 2025", false);
    EXPECT_FALSE(k.preempt_rt);
    EXPECT_EQ(k.preemption_model, "PREEMPT");
}

TEST(RtTestIdentity, ParsesDynamicAndNoneModels)
{
    EXPECT_EQ(identity_from_uname("6.6.78", "#1 SMP PREEMPT_DYNAMIC ...", false).preemption_model, "PREEMPT_DYNAMIC");
    EXPECT_EQ(identity_from_uname("6.6.78", "#1 SMP PREEMPT_NONE ...", false).preemption_model, "PREEMPT_NONE");
    EXPECT_EQ(identity_from_uname("6.6.78", "#1 SMP ...", false).preemption_model, "unknown");
}

// ---------------------------------------------------------------------------
// latency statistics
// ---------------------------------------------------------------------------

TEST(RtTestStats, PercentilesOnKnownVector)
{
    // 100 samples 1..100 µs (values in ns).
    std::vector<int64_t> v;
    for (int64_t i = 1; i <= 100; ++i) { v.push_back(i * 1000); }
    const Dist d = summarize(std::move(v), 10);
    ASSERT_EQ(d.samples, size_t(100));
    EXPECT_DOUBLE_EQ(d.min_us, 1.0);
    EXPECT_DOUBLE_EQ(d.max_us, 100.0);
    EXPECT_DOUBLE_EQ(d.p50_us, 50.0);  // ceil(0.50*100)-1 = 49 -> 50
    EXPECT_DOUBLE_EQ(d.p99_us, 99.0);  // ceil(0.99*100)-1 = 98 -> 99
    EXPECT_DOUBLE_EQ(d.p999_us, 100.0); // ceil(0.999*100)-1 = 99 -> 100
}

TEST(RtTestStats, AverageAndSingleton)
{
    const Dist one = summarize({25000}, 10); // 25 µs
    EXPECT_EQ(one.samples, size_t(1));
    EXPECT_DOUBLE_EQ(one.avg_us, 25.0);
    EXPECT_DOUBLE_EQ(one.max_us, 25.0);
    EXPECT_EQ(one.hist[2], 1); // bucket index 25/10 = 2
}

TEST(RtTestStats, HistogramBucketsAndOverflow)
{
    // 5, 15, 100, 1000 µs with 10 µs buckets -> indices 0, 1, 10, 63 (overflow).
    const Dist d = summarize({5000, 15000, 100000, 1000000}, 10);
    ASSERT_EQ(d.hist.size(), size_t(64));
    EXPECT_EQ(d.hist[0], 1);
    EXPECT_EQ(d.hist[1], 1);
    EXPECT_EQ(d.hist[10], 1);
    EXPECT_EQ(d.hist[63], 1);
    for (size_t i = 0; i < d.hist.size(); ++i)
    {
        if (i == 0 || i == 1 || i == 10 || i == 63) { continue; }
        EXPECT_EQ(d.hist[i], 0) << "bucket " << i;
    }
}

TEST(RtTestStats, EmptyVectorIsSafe)
{
    const Dist d = summarize({}, 10);
    EXPECT_EQ(d.samples, size_t(0));
    EXPECT_EQ(d.hist.size(), size_t(0));
}

// ---------------------------------------------------------------------------
// verdict
// ---------------------------------------------------------------------------

TEST(RtTestVerdict, RtWithinBudgetPasses)
{
    const Verdict v = evaluate_verdict(true, true, 214.3, 500.0);
    EXPECT_TRUE(v.preempt_rt);
    EXPECT_TRUE(v.budget_met);
    EXPECT_NE(v.summary.find("bounded"), std::string::npos);
}

TEST(RtTestVerdict, RtOverBudgetFails)
{
    const Verdict v = evaluate_verdict(true, true, 750.0, 500.0);
    EXPECT_FALSE(v.budget_met);
    EXPECT_NE(v.summary.find("exceeds"), std::string::npos);
}

TEST(RtTestVerdict, NonRtNeverMeetsBudget)
{
    const Verdict v = evaluate_verdict(false, true, 40.0, 500.0);
    EXPECT_FALSE(v.preempt_rt);
    EXPECT_FALSE(v.budget_met);
    EXPECT_NE(v.summary.find("Non-RT"), std::string::npos);
}

TEST(RtTestVerdict, MissingLoadDistIsNoted)
{
    const Verdict v = evaluate_verdict(true, false, 0.0, 500.0);
    EXPECT_FALSE(v.budget_met);
    EXPECT_NE(v.summary.find("did not run"), std::string::npos);
}

// ---------------------------------------------------------------------------
// report serialisation (round-trip)
// ---------------------------------------------------------------------------

TEST(RtTestReport, RoundTripCarriesKeyFields)
{
    Report r;
    r.kernel = identity_from_uname("6.6.78-rt51", "#1 SMP PREEMPT_RT ...", true);
    r.sched.fifo_granted = true;
    r.sched.requested_prio = 80;
    r.sched.granted_prio = 80;
    r.sched.rlimit_rtprio = 80;
    r.sched.cap_sys_nice = true;
    r.sched.rt_runtime_us = 950000;
    r.sched.rt_period_us = 1000000;
    r.irq.cpus = 4;
    r.irq.irq_threads = 40;
    r.wakeup_done = true;
    r.wakeup_idle = summarize({10000, 20000, 30000}, 10);
    r.wakeup_load = summarize({200000, 50000, 80000}, 10);
    r.inversion_done = true;
    r.non_pi_block_us = 980.5;
    r.pi_block_us = 120.25;
    r.ipc_done = true;
    r.ipc = summarize({40000, 50000}, 20);
    r.jitter_done = true;
    r.jitter_rate_hz = 10000.0;
    r.jitter = summarize({15000, 16000}, 5);
    r.warnings.emplace_back("degraded: example");
    r.verdict = evaluate_verdict(true, true, r.wakeup_load.max_us, 500.0);

    const JValue j = report_json(r);
    const std::string text = j.dump(2);
    const Result<JValue> parsed = json_parse(text);
    ASSERT_TRUE(parsed.ok()) << text;

    const JValue& back = *parsed;
    ASSERT_TRUE(back.is_object());
    const JValue* kernel = back.find("kernel");
    ASSERT_NE(kernel, nullptr);
    EXPECT_EQ(kernel->get_string("release"), "6.6.78-rt51");
    EXPECT_TRUE(kernel->get_bool("preempt_rt"));
    const JValue* sched = back.find("sched");
    ASSERT_NE(sched, nullptr);
    EXPECT_TRUE(sched->get_bool("fifo_granted"));
    EXPECT_EQ(sched->get_int("granted_prio"), 80);
    EXPECT_EQ(sched->get_int("rt_runtime_us"), 950000);
    const JValue* tests = back.find("tests");
    ASSERT_NE(tests, nullptr);
    const JValue* wakeup = tests->find("wakeup_latency");
    ASSERT_NE(wakeup, nullptr);
    const JValue* load = wakeup->find("load");
    ASSERT_NE(load, nullptr);
    EXPECT_NEAR(load->get_number("max_us"), 200.0, 1e-9);
    EXPECT_EQ(load->get_int("samples"), 3);
    const JValue* inv = tests->find("priority_inversion");
    ASSERT_NE(inv, nullptr);
    EXPECT_NEAR(inv->get_number("non_pi_block_us"), 980.5, 1e-9);
    EXPECT_NEAR(inv->get_number("pi_block_us"), 120.25, 1e-9);
    const JValue* verdict = back.find("verdict");
    ASSERT_NE(verdict, nullptr);
    EXPECT_TRUE(verdict->get_bool("budget_met"));
    EXPECT_EQ(verdict->get_number("budget_max_latency_us"), 500.0);
    const JValue* improves = back.find("rt_improves");
    ASSERT_NE(improves, nullptr);
    EXPECT_EQ(improves->size(), size_t(6));
    const JValue* warnings = back.find("warnings");
    ASSERT_NE(warnings, nullptr);
    ASSERT_EQ(warnings->size(), size_t(1));
    EXPECT_EQ(warnings->items()[0].as_string(), "degraded: example");
    EXPECT_FALSE(back.get_bool("interrupted"));
}

TEST(RtTestReport, DegradedRunRecordsWarning)
{
    Report r;
    r.kernel = identity_from_uname("6.6.78", "#1 SMP PREEMPT_VOLUNTARY ...", false);
    r.sched.fifo_granted = false;
    r.sched.requested_prio = 80;
    r.sched.note = "SCHED_FIFO not granted (Operation not permitted)";
    r.warnings.push_back(r.sched.note);
    const JValue j = report_json(r);
    const Result<JValue> parsed = json_parse(j.dump(2));
    ASSERT_TRUE(parsed.ok());
    const JValue* sched = (*parsed).find("sched");
    ASSERT_NE(sched, nullptr);
    EXPECT_FALSE(sched->get_bool("fifo_granted"));
    EXPECT_EQ(sched->get_string("note"), "SCHED_FIFO not granted (Operation not permitted)");
    const JValue* warnings = (*parsed).find("warnings");
    ASSERT_NE(warnings, nullptr);
    ASSERT_EQ(warnings->size(), size_t(1));
    EXPECT_NE(warnings->items()[0].as_string().find("Operation not permitted"), std::string::npos);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

TEST(RtTestStamp, FormatIsUtcCompact)
{
    const std::string s = utc_stamp();
    ASSERT_EQ(s.size(), size_t(16)); // YYYYMMDDTHHMMSSZ
    EXPECT_EQ(s[15], 'Z');
    EXPECT_EQ(s[8], 'T');
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (i != 8 && i != 15) { EXPECT_TRUE(s[i] >= '0' && s[i] <= '9') << s; }
    }
}