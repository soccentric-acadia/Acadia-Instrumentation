#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

#include "instr/common/log.h"
#include "instr/common/timestamps.h"

namespace
{
std::atomic<bool> stop{false};

void signal_handler(int)
{
    stop = true;
}

bool read_health(const std::string& path, uint64_t& mono_ns)
{
    std::ifstream in(path);
    return static_cast<bool>(in >> mono_ns);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string root = argc > 1 ? argv[1] : "/";
    const std::string run_dir = root + "/run/instrumentation";
    const std::string health_path = run_dir + "/istate.health";
    const std::string log_path = root + "/var/log/instrumentation/istate-watchdog.log";

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    instr::SystemTimeSource ts;
    instr::StderrSink stderr_sink;
    instr::FileSink file_sink(log_path);
    instr::TeeSink sink({&stderr_sink, &file_sink});
    instr::Logger log(sink, ts);

    constexpr uint64_t kCheckIntervalNs = 5'000'000'000ull;
    constexpr uint64_t kFailureAgeNs = 30'000'000'000ull;
    bool outage = false;
    while (!stop.load())
    {
        uint64_t last = 0;
        const uint64_t now = ts.mono_ns();
        const bool present = read_health(health_path, last);
        const bool operational = present && now >= last && now - last <= kFailureAgeNs;
        if (!operational && !outage)
        {
            outage = true;
            log.error("istate-watchdog", "istate is not operational", {{"health_path", health_path}, {"last_mono_ns", std::to_string(last)}});
        }
        else if (operational && outage)
        {
            outage = false;
            log.info("istate-watchdog", "istate operational again", {{"last_mono_ns", std::to_string(last)}});
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(kCheckIntervalNs));
    }
    return 0;
}
