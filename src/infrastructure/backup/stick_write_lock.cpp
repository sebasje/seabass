#include "stick_write_lock.hpp"

#include <filesystem>

#if defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace djconvert::infrastructure::backup
{

namespace fs = std::filesystem;

#if defined(__linux__) || defined(__APPLE__)

StickWriteLock::StickWriteLock(const std::string &lockFilePath) : m_fd(-1), m_path(lockFilePath)
{
    fs::create_directories(fs::path(lockFilePath).parent_path());
    m_fd = ::open(lockFilePath.c_str(), O_CREAT | O_RDWR, 0644);
    if (m_fd < 0) {
        throw std::runtime_error("Could not open stick lock file: " + lockFilePath);
    }
    if (::flock(m_fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(m_fd);
        m_fd = -1;
        throw StickBusyError(lockFilePath);
    }
}

StickWriteLock::~StickWriteLock()
{
    if (m_fd >= 0) {
        ::flock(m_fd, LOCK_UN);
        ::close(m_fd);
    }
}

#else

// No advisory-lock implementation for this platform yet (tracked
// alongside adding Windows builds -- would use LockFileEx there). This
// constructor always succeeds, so it currently provides no actual
// cross-process protection outside Linux/macOS -- don't remove this
// comment when that changes, callers rely on real exclusion where it's
// implemented.
StickWriteLock::StickWriteLock(const std::string &lockFilePath) : m_fd(-1), m_path(lockFilePath) {}
StickWriteLock::~StickWriteLock() = default;

#endif

}  // namespace djconvert::infrastructure::backup
