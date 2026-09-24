/**
 * @file lib/common/include/instr/common/process.h
 * @brief Run a program by argv — never through a shell (T-P31, INS-007/008).
 *
 * Every former `std::system`/`popen` call built a shell string from inputs
 * (unit names from HQ/ctrl, bootloader variables, device and artifact paths),
 * so a crafted value could inject commands. `run_argv` spawns the program
 * directly (posix_spawnp); arguments are never interpreted.
 *
 * @copyright (C) 2026 Soccentric. All rights reserved. Proprietary.
 */
#pragma once

#include <string>
#include <vector>

namespace instr
{
namespace proc
{

/// Outcome of one child process.
struct ProcResult
{
    bool spawned = false; ///< false when the program could not be started.
    int exit_code = -1;   ///< exit status; -1 when killed by a signal.
    std::string output;   ///< stdout (and stderr when merge_stderr).
};

/**
 * @brief Run argv[0] (PATH lookup) with argv; wait for it.
 * @param argv Program and arguments; must not be empty.
 * @param capture Collect stdout (else it goes to /dev/null).
 * @param merge_stderr Also collect stderr into output (else /dev/null).
 */
ProcResult run_argv(const std::vector<std::string>& argv, bool capture = false, bool merge_stderr = false);

/// systemd unit name check: [A-Za-z0-9@._:-]+ with a unit-type suffix.
bool valid_unit_name(const std::string& unit);

} // namespace proc
} // namespace instr
