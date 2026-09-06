#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "application/use_cases/restore_stick_backup.hpp"
#include "domain/library_fingerprint.hpp"

namespace seabass::application
{

// What the stick list knows about one mounted stick, and every backup in
// the backup folder. Pure input: the GUI gathers it on a worker thread.
struct StickBackupAdviceInput
{
    bool hasLibrary = false;
    std::string stickIdentifier;  // infrastructure::system::StickHardwareInfo::stickIdentifier
    std::string stickLabel;
    std::optional<domain::LibraryFingerprint> liveFingerprint;  // nullopt: no library, or unreadable
    // Archive-relative main-database path -> the DbSetFingerprint hex the
    // stick has right now (absent when that database is not on the stick).
    std::map<std::string, std::string> liveDatabaseFingerprints;
    std::vector<StickBackupDescription> backups;  // unreadable ones (error set) are ignored
};

// Which backup this stick relates to, and what to offer for it. Decided
// by the library's content fingerprint first, the stick's hardware
// identifier when there is no fingerprint to go on (an older backup, an
// empty stick), then the label, then plain recency.
struct StickBackupAdvice
{
    enum class State
    {
        NoBackups,         // the folder has no readable backup at all
        Restore,           // no library on the stick: offer to restore `backupPath`
        BackUpNew,         // a library, but no backup of it: offer a fresh backup
        Current,           // `backupPath` holds this library as it is now
        Outdated,          // `backupPath` holds this library, but it has changed since: offer to update
        DifferentLibrary,  // `backupPath` was taken from this stick, but holds a different library
    };
    enum class MatchedBy
    {
        None,
        Fingerprint,
        Identifier,
        Label,
        Newest,
    };

    State state = State::NoBackups;
    MatchedBy matchedBy = MatchedBy::None;
    std::filesystem::path backupPath;
    std::string backupLabel;
    std::int64_t backupCreatedAtUnix = 0;
    double trackOverlap = -1.0;  // from the fingerprint comparison, -1 when none was possible
    double cueOverlap = -1.0;
    std::string detail;  // one sentence for the stick list
};

StickBackupAdvice adviseStickBackup(const StickBackupAdviceInput &input);

std::string_view toString(StickBackupAdvice::State state);
std::string_view toString(StickBackupAdvice::MatchedBy matchedBy);

}  // namespace seabass::application
