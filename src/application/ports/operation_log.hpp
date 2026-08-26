#pragma once

#include <string>

namespace djconvert::application
{

// Port for a persistent, append-only record of mutating operations
// djconvert performs (writes, backups) -- distinct from the terminal
// report, which is ephemeral. Every write should log what it did here, in
// enough detail to reconstruct it later without djconvert running.
class OperationLog
{
public:
    virtual ~OperationLog() = default;
    virtual void record(const std::string &message) = 0;
};

}  // namespace djconvert::application
