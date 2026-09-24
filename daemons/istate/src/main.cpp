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
 * @file daemons/istate/src/main.cpp
 * @brief istate daemon main (REQ §7.1). The hub: /pst, provisioning, orchestration,
 *
 * config service, spool, HQ session, hub gRPC server, ctrl server.
 *
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */

#include <cstdio>
#include <fstream>
#include <set>
#include <unistd.h>

#include "instr/alarms_gen.h"
#include "instr/common/daemon_skeleton.h"
#include "instr/common/log.h"
#include "instr/common/version_gen.h"
#include "instr/ctrl/ctrl.h"
#include "instr/istate/core.h"
#include "instr/istate/table.h"
#include "instr/istate/hubs.h"
#include "instr/platform/descriptors.h"
#include "instr/platform/factory.h"
#include "instr/common/filesystem.h"
#include <grpcpp/grpcpp.h>

namespace
{
// Default file set shipped in the image (§6: seeds /pst on first boot with
// every default file, not just config.yaml — task 007).
/// @brief Load the complete image default file set for seeding and system reset.
/// @param fs Filesystem to read from.
/// @param dir Directory holding the default file set (one subdirectory per daemon).
/// @return The complete image default file set per daemon.
instr::istate::DefaultFileSet load_defaults(instr::IFileSystem& fs, const std::string& dir)
{
    instr::istate::DefaultFileSet out;
    auto daemons = fs.list_dir(dir);
    if (!daemons.ok()) return out;
    for (const auto& d : *daemons)
    {
        auto files = fs.list_dir(dir + "/" + d);
        if (!files.ok()) continue;
        for (const auto& f : *files)
        {
            if (!f.empty() && f.front() == '.') continue;   // e.g. .gitkeep
            auto txt = fs.read(dir + "/" + d + "/" + f);
            if (txt.ok()) out[d][f] = *txt;
        }
    }
    return out;
}

// gRPC servers are truthy via operator bool.
/// @brief True when the gRPC server has started.
/// @param s Server to check.
/// @return True when BuildAndStart succeeded.
inline bool server_up(const std::unique_ptr<grpc::Server>& s)
{
    return s != nullptr;
}
} // namespace

/// @brief istate entry: parse args, boot the core, serve until stopped.
int main(int argc, char** argv)
{
    auto args = instr::parse_daemon_args(argc, argv, "istate");
    if (args.help)
    {
        std::printf("istate — the hub: state, provisioning, config, orchestration, external face\n"
                    "usage: istate [--socket PATH] [--root PATH] [--help]\n");
        return 0;
    }
    std::atomic<bool> stop{false};
    instr::install_signal_handlers(stop);
    instr::StderrSink stderr_sink;
    const std::string log_root = args.root.empty() ? "/var/log/instrumentation" : args.root + "/var/log/instrumentation";
    instr::FileSink file_sink(log_root + "/istate.log");
    instr::TeeSink sink({&stderr_sink, &file_sink});
    instr::SystemTimeSource ts;
    instr::Logger log(sink, ts);

    std::string root = args.root.empty() ? "/" : args.root;
    std::string run_dir = root + "/run/instrumentation";
    instr::NativeFileSystem fs;
    fs.mkdirs(run_dir);
    const std::string health_path = run_dir + "/istate.health";

    instr::istate::IstateOptions opts;
    opts.pst_root = root + "/pst";
    opts.run_dir = run_dir;
    opts.build_version = INSTR_VERSION;
    // §5: the same binary runs against the simulated platform on a build host.
    bool platform_known = true;
    auto plat_owned = instr::platform::make_platform(args.platform, root, &platform_known);
    if (!platform_known) log.warning(args.name, "unknown --platform '" + args.platform + "', using native");
    instr::platform::IPlatform& plat = *plat_owned;
    instr::istate::IstateCore core(opts, fs, plat, ts, log);

    auto defaults = load_defaults(fs, root + "/usr/share/instrumentation/defaults");
    core.set_default_provider([defaults] { return defaults; });
    // Capability manifest (§5.1): the board's descriptor is the source of
    // truth for what this target has, so istate reports honestly instead of
    // accepting a call it cannot serve.
    {
        auto caps_txt = fs.read(root + "/etc/instrumentation/capabilities.yaml");
        auto caps_schema_txt = fs.read(root + "/usr/share/instrumentation/schemas/capability.schema.json");
        instr::JValue caps_schema = instr::JValue::object();
        if (caps_schema_txt.ok())
        {
            auto parsed = instr::load_schema_text(*caps_schema_txt);
            if (parsed.ok()) caps_schema = *parsed;
        }
        if (caps_txt.ok())
        {
            auto manifest = instr::platform::load_capability_manifest(*caps_txt, caps_schema, "capabilities.yaml");
            if (manifest.ok()) { core.set_capabilities(manifest->capabilities); }
            else { log.error("istate", "capability manifest rejected: " + manifest.error.str() + " — reporting no optional capabilities"); }
        }
        else { log.warning("istate", "no capability manifest installed — reporting no optional capabilities"); }
    }

    auto table = build_istate_table(&core);
    std::string ctrl_path = args.socket_override.empty() ? run_dir + "/istate-ctrl.sock" : args.socket_override;
    instr::ctrl::CtrlServer ctrl(&table, ctrl_path);
    auto e = ctrl.start();
    if (!e.ok())
    {
        std::fprintf(stderr, "istate: ctrl server failed: %s\n", e.str().c_str());
        return 1;
    }

    std::vector<std::unique_ptr<grpc::Service>> services;
    auto server = instr::istate::build_hub_server(core, run_dir, &services);
    if (!server_up(server))
    {
        std::fprintf(stderr, "istate: gRPC server failed to start\n");
        return 1;
    }

    // HQ session: last in the startup order, never a precondition (§7.1).
    // Gate on a provisioned transport (task 009, H11): the default dial target
    // is a local bridge socket that no production component serves — retrying
    // forever against a socket that never exists was noise, and hid the fact
    // that the HqPeer path has no HQ-side server. With INSTR_HQ_TARGET set, or
    // the bridge socket present, the session starts; otherwise istate runs
    // disconnected, honestly, and the northbound spool retains until one is
    // provisioned.
    std::string hq_target = "unix:" + run_dir + "/hq.sock";
    if (const char* env = ::getenv("INSTR_HQ_TARGET")) hq_target = env;
    const bool hq_transport_provisioned = ::getenv("INSTR_HQ_TARGET") != nullptr || ::access((run_dir + "/hq.sock").c_str(), F_OK) == 0;
    if (!hq_transport_provisioned) { log.warning(args.name, "HQ transport not provisioned (no INSTR_HQ_TARGET, no " + run_dir + "/hq.sock) — northbound session disabled; provision a bridge or tunnel target to enable it"); }
    instr::istate::HqClient hq(core, log, hq_target);
    if (hq_transport_provisioned) hq.start();

    core.boot(defaults);
    instr::sd_notify_ready();

    // Liveness sweeps + watchdog petting on the main loop. The sweep cadence
    // is istate's heartbeat_interval_s (§3.1); the health marker is written
    // every iteration (1 s) so the software watchdog's 30 s staleness window
    // never trips on a long configured heartbeat interval.
    double last_sweep_s = -1.0;
    instr::run_until_stop(
        stop,
        [&] {
            // The independent software watchdog observes this atomically
            // replaced progress marker. It has no reboot or restart path.
            {
                const std::string tmp = health_path + ".tmp";
                std::ofstream health(tmp, std::ios::trunc);
                if (health)
                {
                    health << ts.mono_ns() << '\n';
                    health.flush();
                    health.close();
                    if (std::rename(tmp.c_str(), health_path.c_str()) != 0) log.warning("istate", "software watchdog health marker replace failed");
                }
                else { log.warning("istate", "software watchdog health marker unavailable", {{"path", health_path}}); }
            }
            const auto cur = core.options();
            core.orchestrator().set_watchdog_grace_s(cur.liveness_hang_timeout_s);
            const double now_s = double(ts.mono_ns()) / 1e9;
            if (last_sweep_s < 0.0 || now_s - last_sweep_s >= cur.heartbeat_interval_s)
            {
                last_sweep_s = now_s;
                auto evs = core.orchestrator().liveness_sweep(now_s, cur.liveness_hang_timeout_s, cur.heartbeat_warning_timeout_s);
                for (const auto& ev : evs)
                {
                    log.warning("istate", "liveness: " + ev.daemon + " " + ev.event);
                    // Crash forensics (§7.7): capture on abnormal daemon exit. The
                    // capture itself lives in logmanager; istate is what notices, so
                    // it is istate that has to ask. Without this the forensic path
                    // only ever fired when an operator ran the ctrl op by hand —
                    // long after the evidence it exists for was gone.
                    if (ev.event == "hung" || ev.event == "degraded")
                    {
                        instr::JValue crash_args = instr::JValue::object();
                        crash_args.set("daemon", instr::JValue(ev.daemon));
                        // Empty coredump path: logmanager resolves it from the
                        // platform's coredump directory.
                        crash_args.set("tail_lines", instr::JValue(200));
                        instr::common::Command cmd;
                        cmd.set_op("crash-report");
                        cmd.set_json_args(crash_args.dump());
                        cmd.set_issued_by("istate");
                        // Fire and forget: a forensic capture must never hold up the
                        // liveness sweep or the watchdog pet (R10).
                        (void)core.relay_command("logmanager", cmd);
                    }
                }
            }
            core.orchestrator().pet_watchdog();

            // §6: "an alarm raised before the limit". A log line is not an alarm —
            // nobody is watching the journal on a fielded device, and the whole
            // point of the budget is to be visible before storage wedges anything.
            // Edge-triggered so a standing condition does not re-raise every second.
            static std::set<std::string> warned, exceeded;
            auto sync = [&](const std::vector<std::string>& now, std::set<std::string>& seen, uint32_t alarm_id, const char* what) {
                std::set<std::string> current(now.begin(), now.end());
                for (const auto& c : current)
                {
                    if (seen.insert(c).second)
                    {
                        log.warning("istate", std::string("storage budget ") + what + ": " + c);
                        core.raise_alarm(alarm_id, std::string("consumer '") + c + "' " + what);
                    }
                }
                for (auto it = seen.begin(); it != seen.end();)
                {
                    if (current.count(*it)) { ++it; }
                    else
                    {
                        log.info("istate", std::string("storage budget recovered: ") + *it);
                        it = seen.erase(it);
                    }
                }
                if (current.empty() && !seen.empty()) seen.clear();
                if (current.empty()) core.clear_alarm(alarm_id);
            };
            sync(core.budgets().over_warning(), warned, instr::INSTR_ALARM_ISTATE_BUDGET_WARNING, "warning");
            sync(core.budgets().over_limit(), exceeded, instr::INSTR_ALARM_ISTATE_BUDGET_EXCEEDED, "exceeded");
            return true;
        },
        1000);

    core.shutdown();
    hq.stop();
    server->Shutdown();
    ctrl.stop();
    return 0;
}