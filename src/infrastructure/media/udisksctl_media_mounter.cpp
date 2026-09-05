#include "infrastructure/media/udisksctl_media_mounter.hpp"

#include "infrastructure/process/run_command.hpp"

namespace seabass::infrastructure::media
{

using process::runCommand;

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

}  // namespace seabass::infrastructure::media
