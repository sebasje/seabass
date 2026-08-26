#include "infrastructure/logging/file_operation_log.hpp"

#include <chrono>
#include <fstream>
#include <format>

namespace djconvert::infrastructure::logging
{

namespace
{

std::string timestampNow()
{
    return std::format("{:%Y-%m-%dT%H:%M:%S}", std::chrono::floor<std::chrono::seconds>(
                                                     std::chrono::system_clock::now()));
}

}  // namespace

FileOperationLog::FileOperationLog(std::string logFilePath) : m_logFilePath(std::move(logFilePath)) {}

void FileOperationLog::record(const std::string &message)
{
    std::ofstream out(m_logFilePath, std::ios::app);
    out << timestampNow() << "  " << message << "\n";
}

}  // namespace djconvert::infrastructure::logging
