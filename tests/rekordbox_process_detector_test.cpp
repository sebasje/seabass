#include <cassert>
#include <iostream>
#include <string>

#include <unistd.h>

#include "infrastructure/system/rekordbox_process_detector.hpp"

using namespace djconvert::infrastructure::system;

int main()
{
    // Self-detection: this very test process should find itself running,
    // proving the scanner actually inspects real process state rather
    // than trivially returning false for everything.
    char selfExe[4096] = {};
    ssize_t n = readlink("/proc/self/exe", selfExe, sizeof(selfExe) - 1);
    assert(n > 0);
    std::string exePath(selfExe, static_cast<size_t>(n));
    std::string baseName = exePath.substr(exePath.find_last_of('/') + 1);
    // /proc/<pid>/comm truncates to 15 bytes -- match what the detector
    // itself reads, not the (possibly longer) real binary name.
    if (baseName.size() > 15) {
        baseName = baseName.substr(0, 15);
    }
    assert(isProcessRunning(baseName));
    std::cout << "case 1 (self-detection via /proc) OK\n";

    assert(!isProcessRunning("definitely-not-a-real-process-xyz123"));
    std::cout << "case 2 (nonexistent process not detected) OK\n";

    // Not a hard assertion -- rekordbox could theoretically be running via
    // Wine on this machine -- but flagging it is more useful than silently
    // passing if the detector is ever broken in a way that always returns
    // true.
    if (isRekordboxRunning()) {
        std::cerr << "warning: isRekordboxRunning() returned true in the test environment\n";
    }
    std::cout << "case 3 (isRekordboxRunning wrapper runs without error) OK\n";

    std::cout << "all cases passed\n";
    return 0;
}
