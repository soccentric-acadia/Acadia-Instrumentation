/**
 * @file lib/common/src/process.cpp
 * @brief argv-only process execution (T-P31, INS-007/008).
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */
#include "instr/common/process.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>

extern char** environ;

namespace instr
{
namespace proc
{

ProcResult run_argv(const std::vector<std::string>& argv, bool capture, bool merge_stderr)
{
    ProcResult r;
    if (argv.empty()) return r;
    int fds[2] = {-1, -1};
    if (capture && ::pipe2(fds, O_CLOEXEC) != 0) return r;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (capture)
    {
        posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
        if (merge_stderr)
            posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
        else
            posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    else
    {
        posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = -1;
    const int rc = ::posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (capture) ::close(fds[1]);
    if (rc != 0)
    {
        if (capture) ::close(fds[0]);
        return r;
    }
    r.spawned = true;
    if (capture)
    {
        char buf[4096];
        for (;;)
        {
            ssize_t n = ::read(fds[0], buf, sizeof buf);
            if (n > 0)
                r.output.append(buf, size_t(n));
            else if (n < 0 && errno == EINTR)
                continue;
            else
                break;
        }
        ::close(fds[0]);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

bool valid_unit_name(const std::string& unit)
{
    static const char* kSuffixes[] = {".service", ".timer", ".socket", ".target", ".path", ".mount"};
    if (unit.empty() || unit.size() > 255 || unit[0] == '-') return false;
    for (char c : unit)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '@' || c == '.' || c == '_' || c == '-' || c == ':')) return false;
    for (const char* s : kSuffixes)
    {
        const std::string suf(s);
        if (unit.size() > suf.size() && unit.compare(unit.size() - suf.size(), suf.size(), suf) == 0) return true;
    }
    return false;
}

} // namespace proc
} // namespace instr
