#pragma once

#include <string>

#include "application/ports/operation_log.hpp"

namespace djconvert::infrastructure::logging
{

// Appends timestamped lines to a plain text log file. Each call opens,
// appends, and closes the file, so multiple djconvert processes writing to
// the same stick won't stomp on each other's log lines.
class FileOperationLog : public application::OperationLog
{
public:
    explicit FileOperationLog(std::string logFilePath);

    void record(const std::string &message) override;

private:
    std::string m_logFilePath;
};

}  // namespace djconvert::infrastructure::logging
