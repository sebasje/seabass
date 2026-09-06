#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "application/use_cases/advise_stick_backup.hpp"

using namespace seabass::application;
using namespace seabass::domain;

namespace
{

std::vector<Track> makeLibrary(std::size_t count, int seed)
{
    std::vector<Track> tracks;
    for (std::size_t i = 0; i < count; ++i) {
        Track track;
        track.title = "Track " + std::to_string(i + seed * 100000);
        track.artist = "Artist " + std::to_string((i + seed) % 7);
        track.durationSeconds = 180.0 + static_cast<double>(i % 200);
        CuePoint cue;
        cue.positionMs = 1000.0 * static_cast<double>(1 + i % 5);
        track.cues.push_back(cue);
        tracks.push_back(track);
    }
    return tracks;
}

StickBackupDescription makeBackup(const std::string &path, const std::string &identifier, const std::string &label,
                                  std::int64_t createdAt, const std::vector<Track> &library)
{
    StickBackupDescription backup;
    backup.archivePath = path;
    backup.stickIdentifier = identifier;
    backup.stickLabel = label;
    backup.createdAtUnix = createdAt;
    backup.libraryFingerprint = fingerprintLibrary(library).serialize();
    backup.databaseFingerprints = {{"PIONEER/rekordbox/export.pdb", "db-" + path}};
    return backup;
}

}  // namespace

int main()
{
    const std::vector<Track> libraryA = makeLibrary(300, 1);
    const std::vector<Track> libraryB = makeLibrary(300, 2);
    const StickBackupDescription backupA = makeBackup("/b/A.zip", "uuid-a", "STICKA", 200, libraryA);
    const StickBackupDescription backupB = makeBackup("/b/B.zip", "uuid-b", "STICKB", 100, libraryB);

    // Nothing in the folder.
    {
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        assert(adviseStickBackup(input).state == StickBackupAdvice::State::NoBackups);
        StickBackupDescription broken;
        broken.error = "unreadable";
        input.backups = {broken};
        assert(adviseStickBackup(input).state == StickBackupAdvice::State::NoBackups);
    }

    // Empty stick: its own backup by identifier, else by label, else the newest.
    {
        StickBackupAdviceInput input;
        input.hasLibrary = false;
        input.backups = {backupA, backupB};
        input.stickIdentifier = "uuid-b";
        StickBackupAdvice advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::Restore);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Identifier);
        assert(advice.backupPath == "/b/B.zip");

        input.stickIdentifier = "uuid-fresh-after-format";
        input.stickLabel = "STICKB";
        advice = adviseStickBackup(input);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Label);
        assert(advice.backupPath == "/b/B.zip");

        input.stickLabel = "NEW";
        advice = adviseStickBackup(input);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Newest);
        assert(advice.backupPath == "/b/A.zip");
    }

    // The same library on a different stick (re-imported, new UUID): found
    // by content, current when the database fingerprint still matches.
    {
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        input.stickIdentifier = "uuid-other";
        input.liveFingerprint = fingerprintLibrary(libraryB);
        input.liveDatabaseFingerprints = {{"PIONEER/rekordbox/export.pdb", "db-/b/B.zip"}};
        input.backups = {backupA, backupB};
        StickBackupAdvice advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::Current);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Fingerprint);
        assert(advice.backupPath == "/b/B.zip");
        assert(advice.trackOverlap == 1.0);

        input.liveDatabaseFingerprints = {{"PIONEER/rekordbox/export.pdb", "db-changed"}};
        advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::Outdated);
        assert(advice.backupPath == "/b/B.zip");
    }

    // Grown since the backup: still that backup, outdated.
    {
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        input.liveFingerprint = fingerprintLibrary(makeLibrary(400, 2));
        input.backups = {backupA, backupB};
        StickBackupAdvice advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::Outdated);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Fingerprint);
        assert(advice.backupPath == "/b/B.zip");
    }

    // A library nobody has backed up.
    {
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        input.liveFingerprint = fingerprintLibrary(makeLibrary(300, 3));
        input.backups = {backupA, backupB};
        assert(adviseStickBackup(input).state == StickBackupAdvice::State::BackUpNew);
    }

    // The stick that backup A came from now carries library B: a different
    // library, but B's own backup wins over the identifier.
    {
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        input.stickIdentifier = "uuid-a";
        input.liveFingerprint = fingerprintLibrary(libraryB);
        input.backups = {backupA, backupB};
        StickBackupAdvice advice = adviseStickBackup(input);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Fingerprint);
        assert(advice.backupPath == "/b/B.zip");

        input.liveFingerprint = fingerprintLibrary(makeLibrary(300, 4));
        advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::DifferentLibrary);
        assert(advice.backupPath == "/b/A.zip");
    }

    // An old backup without a content fingerprint: the identifier decides,
    // and the database fingerprint says whether it is current.
    {
        StickBackupDescription old = backupA;
        old.libraryFingerprint.clear();
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        input.stickIdentifier = "uuid-a";
        input.liveFingerprint = fingerprintLibrary(libraryA);
        input.liveDatabaseFingerprints = {{"PIONEER/rekordbox/export.pdb", "db-/b/A.zip"}};
        input.backups = {old};
        StickBackupAdvice advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::Current);
        assert(advice.matchedBy == StickBackupAdvice::MatchedBy::Identifier);
    }

    // Same tracks but the cues were lost: outdated with a cue-specific hint.
    {
        std::vector<Track> noCues = libraryA;
        for (Track &track : noCues) {
            track.cues.clear();
        }
        StickBackupAdviceInput input;
        input.hasLibrary = true;
        input.liveFingerprint = fingerprintLibrary(noCues);
        input.backups = {backupA};
        StickBackupAdvice advice = adviseStickBackup(input);
        assert(advice.state == StickBackupAdvice::State::Outdated);
        assert(advice.detail.find("cues") != std::string::npos);
    }

    std::cout << "All advise_stick_backup tests passed." << std::endl;
    return 0;
}
