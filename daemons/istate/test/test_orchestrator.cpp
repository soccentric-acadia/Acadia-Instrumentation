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
 * @file test/istate/test_orchestrator.cpp
 * @brief orchestrator tests (REQ §7.1): startup order, registration with
 *
 * proto verification, liveness/hang detection, watchdog policy.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */

#include <gtest/gtest.h>

#include "instr/istate/orchestrator.h"
#include "instr/common/json.h"
#include "instr/transport/transport.h"
#include "instr/platform/simulated.h"

using namespace instr;
using namespace instr::istate;
using namespace instr::platform;

/// @brief The declared startup order matches the §7.1 spec exactly.
TEST(OrchestratorOrder, StartupOrderMatchesSpec)
{
    auto& order = startup_order();
    ASSERT_EQ(order.size(), 9u);
    // Observation daemons start early (§7.1).
    EXPECT_EQ(order[0], "faults");
    EXPECT_EQ(order[1], "monitord");
    EXPECT_EQ(order[2], "hardware");
    // HQ-facing daemons last; OTA/Backup after networkd (tunnel).
    auto pos = [&](const std::string& d) { return std::find(order.begin(), order.end(), d) - order.begin(); };
    EXPECT_GT(pos("ota"), pos("networkd"));
    EXPECT_GT(pos("backup"), pos("networkd"));
    EXPECT_EQ(pos("alarms"), 3u);
    EXPECT_EQ(pos("telemetry"), 5u);
}

/// @brief Dependency edges reference declared spokes only and are consistent.
TEST(OrchestratorOrder, DependencyEdgesConsistent)
{
    auto& order = startup_order();
    for (const auto& kv : dependency_edges())
    {
        size_t self = std::find(order.begin(), order.end(), kv.first) - order.begin();
        for (const auto& dep : kv.second)
        {
            size_t di = std::find(order.begin(), order.end(), dep) - order.begin();
            EXPECT_LT(di, self) << dep << " must start before " << kv.first;
        }
    }
}

TEST(Orchestrator, HealthSummaryAggregatesPresence)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes({"alpha", "beta"});
    orch.mark_ready("alpha");
    auto summary = json_parse(orch.health_summary_json());
    ASSERT_TRUE(summary.ok());
    EXPECT_EQ((*summary).find("total")->as_number(), 2);
    EXPECT_EQ((*summary).find("ready")->as_number(), 1);
    EXPECT_EQ((*summary).find("not_present")->as_number(), 1);
    EXPECT_EQ((*summary).find("state")->as_string(), "degraded");
    EXPECT_EQ((*summary).find("peers")->size(), 2u);
}

/// @brief A proto mismatch at registration is refused and alarms.
TEST(Orchestrator, RegistrationRefusesProtoMismatch)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes(startup_order());
    std::string reason;
    auto ok = orch.register_spoke("monitord", "1.0.0", instr::transport::kProtoSchemaVersion, {"samples"}, &reason);
    EXPECT_EQ(ok, Orchestrator::Reg::Accepted);
    orch.mark_ready("monitord");

    auto bad = orch.register_spoke("faults", "1.0.0", 999, {}, &reason);
    EXPECT_EQ(bad, Orchestrator::Reg::SchemaMismatch);
    EXPECT_NE(reason.find("mismatch"), std::string::npos);
    // An alarm event was emitted for the refusal (§3.1: refuse + alarm).
    // (verified via the event sink in the integration harness)

    auto unknown = orch.register_spoke("ghost", "1.0.0", 1, {}, &reason);
    EXPECT_EQ(unknown, Orchestrator::Reg::NameMismatch);
}

/// @brief A spoke past the hang timeout is marked Hung and a restart is requested.
TEST(Orchestrator, HangDetectionRequestsRestart)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes({"monitord"});
    orch.register_spoke("monitord", "1.0.0", 1, {});
    orch.mark_ready("monitord");
    bool probe_alive = false; // hung: probe always fails
    orch.set_probe_fn("monitord", [&] { return probe_alive; });

    // First sweep after the hang timeout: detected + restart requested.
    ts.mono += 31ull * 1'000'000'000ull; // > 30s hang timeout
    auto evs = orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30);
    bool saw_hung = false, saw_restart = false;
    for (const auto& e : evs)
    {
        if (e.event == "hung") saw_hung = true;
        if (e.event == "restarted") saw_restart = true;
    }
    EXPECT_TRUE(saw_hung);
    EXPECT_TRUE(saw_restart);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Hung);
    // The process that is running but not responding does NOT look healthy.
    EXPECT_FALSE((*orch.spoke("monitord"))->live);
    EXPECT_EQ(plat.unit_restarts().size(), 1u); // restart requested via systemd
}

/// @brief A spoke answering again recovers to Ready.
TEST(Orchestrator, RecoversWhenProbeRespondsAgain)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes({"monitord"});
    orch.register_spoke("monitord", "1.0.0", 1, {});
    orch.mark_ready("monitord");
    bool alive = false;
    orch.set_probe_fn("monitord", [&] { return alive; });
    ts.mono += 40ull * 1'000'000'000ull;
    orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Hung);
    alive = true;
    auto evs = orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Ready);
}

/// @brief The watchdog is petted while must-live daemons are healthy; a down
/// daemon gets a grace window (a restart takes seconds; a transient must not
/// reboot the device), after which petting stops so the hardware watchdog can
/// act as the last resort (§7.1).
TEST(Orchestrator, WatchdogPetsOnlyWhenMustLiveHealthy)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes({"faults", "monitord", "hardware", "networkd"});
    for (const auto& n : {"faults", "monitord", "hardware", "networkd"})
    {
        orch.register_spoke(n, "1.0.0", 1, {});
        orch.mark_ready(n);
    }
    orch.set_watchdog_required({"faults", "monitord", "hardware", "networkd"});
    EXPECT_TRUE(orch.pet_watchdog());
    EXPECT_EQ(plat.watchdog_pets(), 1u);
    // A down must-live daemon is petted through the grace window...
    orch.mark_stopped("monitord");
    orch.set_watchdog_grace_s(10.0);
    ts.mono += 5ull * 1'000'000'000ull; // inside grace
    EXPECT_TRUE(orch.pet_watchdog());
    EXPECT_EQ(plat.watchdog_pets(), 2u);
    // ...then petting stops: the hardware watchdog is the last resort.
    ts.mono += 11ull * 1'000'000'000ull; // past grace
    EXPECT_FALSE(orch.pet_watchdog());
    EXPECT_EQ(plat.watchdog_pets(), 2u); // no additional pet
    // Recovery resets the grace window.
    orch.mark_ready("monitord");
    EXPECT_TRUE(orch.pet_watchdog());
    EXPECT_EQ(plat.watchdog_pets(), 3u);
    // Zero grace = the strict policy: petting stops on the first down cycle.
    orch.mark_stopped("monitord");
    orch.set_watchdog_grace_s(0.0);
    EXPECT_FALSE(orch.pet_watchdog());
    EXPECT_EQ(plat.watchdog_pets(), 3u);
}

/// @brief A spoke quiet past the warning threshold (but under the hang
/// threshold) degrades rather than hangs, and recovers when it answers (§3.1).
TEST(Orchestrator, WarningTierDegradesBeforeHang)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes({"monitord"});
    orch.register_spoke("monitord", "1.0.0", 1, {});
    orch.mark_ready("monitord");
    // Pings succeed; the spoke never answers: silence grows unboundedly.
    orch.set_ping_fn([](const std::string&, uint64_t) { return true; });
    ts.mono += 18ull * 1'000'000'000ull; // > 15s warning, < 30s hang
    auto evs = orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30, 15);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Degraded);
    EXPECT_TRUE(orch.degraded());
    bool saw_degraded = false;
    for (const auto& e : evs)
        if (e.event == "degraded") saw_degraded = true;
    EXPECT_TRUE(saw_degraded);
    // Still past warning but under hang: it degrades, it does not hang.
    ts.mono += 5ull * 1'000'000'000ull;
    evs = orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30, 15);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Degraded);
    // Past the hang threshold: hung (the tier is between healthy and hung).
    ts.mono += 10ull * 1'000'000'000ull;
    evs = orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30, 15);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Hung);
    // Answering again recovers through Ready.
    orch.note_pong("monitord", 1);
    evs = orch.liveness_sweep(double(ts.mono_ns()) / 1e9, 30, 15);
    EXPECT_EQ((*orch.spoke("monitord"))->state, SpokeState::Ready);
    EXPECT_FALSE(orch.degraded());
}

/// @brief start_all requests starts through systemd in dependency order.
TEST(Orchestrator, StartAllRequestsThroughSystemd)
{
    SimulatedPlatform plat;
    ManualTimeSource ts;
    Orchestrator orch(plat, ts);
    orch.declare_spokes(startup_order());
    EXPECT_TRUE(orch.start_all());
    for (const auto& n : startup_order())
    {
        auto active = plat.unit_active("instr-" + n + ".service");
        ASSERT_TRUE(active.ok());
        EXPECT_TRUE(*active) << n;
    }
    EXPECT_EQ(plat.started_units().size(), 9u);
}
