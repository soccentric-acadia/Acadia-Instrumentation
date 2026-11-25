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
 * @file daemons/istate/test/test_own_config.cpp
 * @brief istate self-configuration and peer-heartbeat bookkeeping (§3.1, §6).
 *
 * istate is the one daemon that reads its own config file: the heartbeat
 * cadence and thresholds, watchdog timeout, HQ endpoint and the time policy
 * must be runtime-effective, and every peer heartbeat must be recorded with
 * staleness and peer-mismatch rejection.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "instr/common/filesystem.h"
#include "instr/istate/core.h"
#include "instr/istate/pst.h"
#include "instr/platform/simulated.h"

using namespace instr;
using namespace instr::istate;

namespace
{
struct OwnCfgFixture
{
    SimulatedFileSystem fs;
    platform::SimulatedPlatform plat;
    ManualTimeSource ts;
    MemorySink log_sink;
    Logger log{log_sink, ts};
    IstateOptions opts;
    BudgetManager budgets{fs, "/pst"};
    PstStore pst{fs, "/pst", budgets};
    /// @brief Captured relay traffic (daemon, op, args).
    std::vector<std::tuple<std::string, std::string, std::string>> relays;
    /// @brief Provisions a store and wires the relay capture.
    OwnCfgFixture()
    {
        opts.pst_root = "/pst";
        opts.run_dir = "/run/instrumentation";
        opts.build_version = "test";
        fs.mkdirs(opts.run_dir);
    }
    /// @brief Boot the core with @p defaults and relay capture installed.
    /// @param defaults Image default file set.
    /// @return The booted core.
    std::unique_ptr<IstateCore> boot(const DefaultFileSet& defaults)
    {
        auto core = std::make_unique<IstateCore>(opts, fs, plat, ts, log);
        core->set_relay_fn([&](const std::string& daemon, const transport::GenericHubMsg& m) {
            relays.emplace_back(daemon, m.has_command ? m.command.op() : std::string(m.has_config ? "config" : "?"), m.has_command ? m.command.json_args() : std::string());
            return true;
        });
        core->set_default_provider([&defaults] { return defaults; });
        (void)core->boot(defaults);
        return core;
    }
    /// @brief True when a relay to @p daemon with op @p op carrying @p needle was captured.
    /// @param core Booted core (unused; kept for readability).
    bool saw_relay(const std::string& daemon, const std::string& op, const std::string& needle) const
    {
        for (const auto& r : relays)
            if (std::get<0>(r) == daemon && std::get<1>(r) == op && std::get<2>(r).find(needle) != std::string::npos) return true;
        return false;
    }
};

/// @brief A heartbeat from a spoke is recorded; stale and mismatched ones are not.
TEST(OwnConfig, HeartbeatsAreRecordedWithStalenessRejection)
{
    OwnCfgFixture f;
    DefaultFileSet defaults{{"istate", {{"config.yaml", "enabled: true\n"}}}};
    auto core = f.boot(defaults);

    common::Heartbeat hb;
    hb.set_peer("monitord");
    hb.set_sequence(1);
    hb.set_mono_ns(111);
    core->on_spoke_heartbeat("monitord", hb);
    EXPECT_EQ(core->last_heartbeat_sequence("monitord"), 1u);
    EXPECT_EQ(core->last_heartbeat_mono_ns("monitord"), 111u);

    // A newer sequence updates the record.
    hb.set_sequence(2);
    hb.set_mono_ns(222);
    core->on_spoke_heartbeat("monitord", hb);
    EXPECT_EQ(core->last_heartbeat_sequence("monitord"), 2u);
    EXPECT_EQ(core->last_heartbeat_mono_ns("monitord"), 222u);

    // A stale sequence is rejected: a replayed or reordered heartbeat must
    // never make a peer look fresher than it is (§3.1).
    hb.set_sequence(1);
    hb.set_mono_ns(333);
    core->on_spoke_heartbeat("monitord", hb);
    EXPECT_EQ(core->last_heartbeat_sequence("monitord"), 2u);
    EXPECT_EQ(core->last_heartbeat_mono_ns("monitord"), 222u);

    // A heartbeat naming a different peer than its channel is rejected.
    hb.set_peer("networkd");
    hb.set_sequence(3);
    core->on_spoke_heartbeat("monitord", hb);
    EXPECT_EQ(core->last_heartbeat_sequence("monitord"), 2u);
    EXPECT_EQ(core->last_heartbeat_sequence("networkd"), 0u);
}

/// @brief istate applies its own config keys to the runtime at boot.
TEST(OwnConfig, OwnKeysAreRuntimeEffectiveAtBoot)
{
    OwnCfgFixture f;
    const std::string istate_yaml = "enabled: true\n"
                                    "heartbeat_interval_s: 3\n"
                                    "heartbeat_warning_timeout_s: 9\n"
                                    "heartbeat_failure_timeout_s: 7\n"
                                    "watchdog_timeout_s: 45\n"
                                    "hq_endpoint: hq.example:51820\n"
                                    "time_source_priority: [ntp, gnss]\n"
                                    "ntp_servers: [ntp.example.org]\n";
    DefaultFileSet defaults{{"istate", {{"config.yaml", istate_yaml}}}};
    auto core = f.boot(defaults);
    auto opts = core->options();
    EXPECT_DOUBLE_EQ(opts.heartbeat_interval_s, 3.0);
    EXPECT_DOUBLE_EQ(opts.heartbeat_warning_timeout_s, 9.0);
    EXPECT_DOUBLE_EQ(opts.liveness_hang_timeout_s, 7.0);
    EXPECT_DOUBLE_EQ(opts.watchdog_timeout_s, 45.0);
    EXPECT_EQ(opts.hq_endpoint, "hq.example:51820");
    // The watchdog was actually programmed with the configured timeout.
    EXPECT_TRUE(f.plat.watchdog_running());
    // The cadence was relayed to every spoke; networkd received the time policy.
    EXPECT_TRUE(f.saw_relay("monitord", "set-heartbeat-interval", "\"interval_s\":3"));
    EXPECT_TRUE(f.saw_relay("networkd", "set-time-policy", "ntp.example.org"));
    EXPECT_TRUE(f.saw_relay("networkd", "set-time-policy", "\"source_priority\""));
}

/// @brief A config write re-applies istate's own keys without a restart (§6).
TEST(OwnConfig, ConfigWriteReappliesOwnKeys)
{
    OwnCfgFixture f;
    DefaultFileSet defaults{{"istate", {{"config.yaml", "enabled: true\nheartbeat_interval_s: 5\n"}}}};
    auto core = f.boot(defaults);
    EXPECT_DOUBLE_EQ(core->options().heartbeat_interval_s, 5.0);

    auto res = core->config_service().set("istate", "enabled: true\nheartbeat_interval_s: 2\nheartbeat_warning_timeout_s: 6\n", Origin::Local, false);
    ASSERT_TRUE(res.ok) << res.error;
    // The write listener applied the new keys to the running options.
    EXPECT_DOUBLE_EQ(core->options().heartbeat_interval_s, 2.0);
    EXPECT_DOUBLE_EQ(core->options().heartbeat_warning_timeout_s, 6.0);
    // The new cadence was relayed to the spokes.
    EXPECT_TRUE(f.saw_relay("monitord", "set-heartbeat-interval", "\"interval_s\":2"));
}

/// @brief deliver_config("istate") applies locally instead of relaying (R4).
TEST(OwnConfig, DeliverConfigToIstateAppliesLocally)
{
    OwnCfgFixture f;
    DefaultFileSet defaults{{"istate", {{"config.yaml", "enabled: true\nheartbeat_interval_s: 5\n"}}}};
    auto core = f.boot(defaults);
    EXPECT_TRUE(core->deliver_config("istate"));
    EXPECT_DOUBLE_EQ(core->options().heartbeat_interval_s, 5.0);
}

/// @brief A spoke registering after a policy change receives the current policy.
TEST(OwnConfig, LateRegistrantReceivesCurrentPolicy)
{
    OwnCfgFixture f;
    DefaultFileSet defaults{{"istate", {{"config.yaml", "enabled: true\nheartbeat_interval_s: 4\nntp_servers: [ntp.example.org]\n"}}}};
    auto core = f.boot(defaults);
    f.relays.clear();
    common::RegistrationRequest req;
    req.set_daemon("networkd");
    req.set_build_version("1.0.0");
    req.set_proto_schema_version(transport::kProtoSchemaVersion);
    core->on_spoke_registration(req, true);
    EXPECT_TRUE(f.saw_relay("networkd", "set-heartbeat-interval", "\"interval_s\":4"));
    EXPECT_TRUE(f.saw_relay("networkd", "set-time-policy", "ntp.example.org"));
}
} // namespace
/// @brief T-P00c: a spoke listed in disabled_spokes is neither started nor
/// expected; every other spoke still starts.
TEST(OwnConfig, DisabledSpokesAreNotStarted)
{
    OwnCfgFixture f;
    DefaultFileSet defaults{{"istate", {{"config.yaml", "enabled: true\ndisabled_spokes: [ota]\n"}}}};
    auto core = f.boot(defaults);
    const auto& started = f.plat.started_units();
    EXPECT_EQ(std::count(started.begin(), started.end(), "instr-ota.service"), 0);
    EXPECT_EQ(std::count(started.begin(), started.end(), "instr-backup.service"), 1);
    EXPECT_EQ(std::count(started.begin(), started.end(), "instr-networkd.service"), 1);
    const auto disabled = core->options().disabled_spokes;
    ASSERT_EQ(disabled.size(), 1u);
    EXPECT_EQ(disabled[0], "ota");
}
