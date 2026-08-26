#include "infrastructure/media/udisksctl_media_mounter.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <vector>

namespace djconvert::infrastructure::media
{

namespace
{

struct CommandResult
{
    int exitCode = -1;
    std::string output;
};

// Runs a command directly via fork/exec (no shell involved, so device paths
// never need escaping), capturing its combined stdout+stderr.
//
// argv is built *before* forking: this process also runs a background udev
// monitor thread, so between fork() and exec() the child must not touch the
// heap (or any other lock another thread might have held at fork time) --
// only async-signal-safe calls are safe there. Building argv in the child
// used to risk exactly that kind of deadlock.
CommandResult runCommand(const std::vector<std::string> &args)
{
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        return {-1, "failed to create pipe"};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return {-1, "fork failed"};
    }

    if (pid == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipeFds[1]);
    std::string output;
    std::array<char, 4096> buffer;
    ssize_t n;
    while ((n = read(pipeFds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(n));
    }
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {exitCode, output};
}

}  // namespace

std::optional<std::string> UdisksctlMediaMounter::mount(const std::string &devicePath, std::string &errorMessage)
{
    auto result = runCommand({"udisksctl", "mount", "-b", devicePath, "--no-user-interaction"});
    if (result.exitCode != 0) {
        errorMessage = result.output.empty() ? "udisksctl mount failed" : result.output;
        return std::nullopt;
    }

    // Typical success output: "Mounted /dev/sdb1 at /media/sebas/LABEL.\n"
    auto atPos = result.output.find(" at ");
    if (atPos == std::string::npos) {
        return std::string();
    }
    auto start = atPos + 4;
    auto end = result.output.find_first_of(".\n", start);
    return result.output.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool UdisksctlMediaMounter::unmount(const std::string &devicePath, std::string &errorMessage)
{
    auto result = runCommand({"udisksctl", "unmount", "-b", devicePath, "--no-user-interaction"});
    if (result.exitCode != 0) {
        errorMessage = result.output.empty() ? "udisksctl unmount failed" : result.output;
        return false;
    }
    return true;
}

}  // namespace djconvert::infrastructure::media
