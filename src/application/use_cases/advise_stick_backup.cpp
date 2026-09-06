#include "application/use_cases/advise_stick_backup.hpp"

namespace seabass::application
{

using domain::FingerprintSimilarity;
using domain::LibraryFingerprint;

namespace
{

struct Candidate
{
    const StickBackupDescription *backup = nullptr;
    StickBackupAdvice::MatchedBy matchedBy = StickBackupAdvice::MatchedBy::None;
    FingerprintSimilarity similarity;
};

bool verdictIsSame(FingerprintSimilarity::Verdict verdict)
{
    return verdict == FingerprintSimilarity::Verdict::Same
           || verdict == FingerprintSimilarity::Verdict::SameCollectionDifferentState;
}

// The backup whose content matches best: a plain "same" beats "same
// tracks, different cues", then the higher track overlap.
Candidate bestByFingerprint(const StickBackupAdviceInput &input, const std::vector<const StickBackupDescription *> &readable)
{
    Candidate best;
    if (!input.liveFingerprint) {
        return best;
    }
    for (const StickBackupDescription *backup : readable) {
        const std::optional<LibraryFingerprint> stored = LibraryFingerprint::parse(backup->libraryFingerprint);
        if (!stored) {
            continue;
        }
        const FingerprintSimilarity similarity = domain::compareFingerprints(*input.liveFingerprint, *stored);
        if (!verdictIsSame(similarity.verdict)) {
            continue;
        }
        const bool better = best.backup == nullptr
                            || (similarity.verdict == FingerprintSimilarity::Verdict::Same
                                && best.similarity.verdict != FingerprintSimilarity::Verdict::Same)
                            || (similarity.verdict == best.similarity.verdict
                                && similarity.trackOverlap > best.similarity.trackOverlap);
        if (better) {
            best = {backup, StickBackupAdvice::MatchedBy::Fingerprint, similarity};
        }
    }
    return best;
}

const StickBackupDescription *firstByIdentifier(const StickBackupAdviceInput &input,
                                                const std::vector<const StickBackupDescription *> &readable)
{
    if (input.stickIdentifier.empty()) {
        return nullptr;
    }
    for (const StickBackupDescription *backup : readable) {
        if (backup->stickIdentifier == input.stickIdentifier) {
            return backup;
        }
    }
    return nullptr;
}

const StickBackupDescription *firstByLabel(const StickBackupAdviceInput &input,
                                           const std::vector<const StickBackupDescription *> &readable)
{
    if (input.stickLabel.empty()) {
        return nullptr;
    }
    for (const StickBackupDescription *backup : readable) {
        if (backup->stickLabel == input.stickLabel) {
            return backup;
        }
    }
    return nullptr;
}

// Exact when the backup captured databases: every one of them must still
// be on the stick with the same DbSetFingerprint. Otherwise the content
// fingerprint has to be identical.
bool backupIsCurrent(const StickBackupAdviceInput &input, const StickBackupDescription &backup)
{
    if (!backup.databaseFingerprints.empty()) {
        for (const auto &[path, hex] : backup.databaseFingerprints) {
            const auto live = input.liveDatabaseFingerprints.find(path);
            if (live == input.liveDatabaseFingerprints.end() || live->second != hex) {
                return false;
            }
        }
        return true;
    }
    const std::optional<LibraryFingerprint> stored = LibraryFingerprint::parse(backup.libraryFingerprint);
    return stored && input.liveFingerprint && *stored == *input.liveFingerprint;
}

void fillBackup(StickBackupAdvice &advice, const StickBackupDescription &backup)
{
    advice.backupPath = backup.archivePath;
    advice.backupLabel = backup.stickLabel;
    advice.backupCreatedAtUnix = backup.createdAtUnix;
}

}  // namespace

StickBackupAdvice adviseStickBackup(const StickBackupAdviceInput &input)
{
    StickBackupAdvice advice;
    std::vector<const StickBackupDescription *> readable;
    for (const StickBackupDescription &backup : input.backups) {
        if (backup.error.empty()) {
            readable.push_back(&backup);
        }
    }
    if (readable.empty()) {
        advice.state = StickBackupAdvice::State::NoBackups;
        advice.detail = input.hasLibrary ? "No backup of this library yet." : "No backups to restore yet.";
        return advice;
    }

    if (!input.hasLibrary) {
        advice.state = StickBackupAdvice::State::Restore;
        if (const StickBackupDescription *backup = firstByIdentifier(input, readable)) {
            fillBackup(advice, *backup);
            advice.matchedBy = StickBackupAdvice::MatchedBy::Identifier;
            advice.detail = "This stick's own backup can be restored onto it.";
        } else if (const StickBackupDescription *byLabel = firstByLabel(input, readable)) {
            fillBackup(advice, *byLabel);
            advice.matchedBy = StickBackupAdvice::MatchedBy::Label;
            advice.detail = "A backup with this stick's name can be restored onto it.";
        } else {
            fillBackup(advice, *readable.front());
            advice.matchedBy = StickBackupAdvice::MatchedBy::Newest;
            advice.detail = "The newest backup can be restored onto this empty stick.";
        }
        return advice;
    }

    Candidate candidate = bestByFingerprint(input, readable);
    const StickBackupDescription *byIdentifier = firstByIdentifier(input, readable);
    if (candidate.backup == nullptr && byIdentifier != nullptr) {
        candidate.backup = byIdentifier;
        candidate.matchedBy = StickBackupAdvice::MatchedBy::Identifier;
        const std::optional<LibraryFingerprint> stored = LibraryFingerprint::parse(byIdentifier->libraryFingerprint);
        if (stored && input.liveFingerprint) {
            candidate.similarity = domain::compareFingerprints(*input.liveFingerprint, *stored);
            if (candidate.similarity.verdict == FingerprintSimilarity::Verdict::Different) {
                fillBackup(advice, *byIdentifier);
                advice.state = StickBackupAdvice::State::DifferentLibrary;
                advice.matchedBy = StickBackupAdvice::MatchedBy::Identifier;
                advice.trackOverlap = candidate.similarity.trackOverlap;
                advice.cueOverlap = candidate.similarity.cueOverlap;
                advice.detail = "This stick's previous backup holds a different library.";
                return advice;
            }
        }
    }
    if (candidate.backup == nullptr) {
        advice.state = StickBackupAdvice::State::BackUpNew;
        advice.detail = "No backup of this library yet.";
        return advice;
    }

    fillBackup(advice, *candidate.backup);
    advice.matchedBy = candidate.matchedBy;
    advice.trackOverlap = candidate.similarity.trackOverlap;
    advice.cueOverlap = candidate.similarity.cueOverlap;
    if (backupIsCurrent(input, *candidate.backup)) {
        advice.state = StickBackupAdvice::State::Current;
        advice.detail = "Backup is up to date.";
    } else {
        advice.state = StickBackupAdvice::State::Outdated;
        advice.detail = candidate.similarity.verdict == FingerprintSimilarity::Verdict::SameCollectionDifferentState
                            ? "Same tracks as the backup, but the cues differ: update it to keep them."
                            : "The library has changed since its last backup.";
    }
    return advice;
}

std::string_view toString(StickBackupAdvice::State state)
{
    switch (state) {
    case StickBackupAdvice::State::NoBackups: return "no-backups";
    case StickBackupAdvice::State::Restore: return "restore";
    case StickBackupAdvice::State::BackUpNew: return "back-up-new";
    case StickBackupAdvice::State::Current: return "current";
    case StickBackupAdvice::State::Outdated: return "outdated";
    case StickBackupAdvice::State::DifferentLibrary: return "different-library";
    }
    return "no-backups";
}

std::string_view toString(StickBackupAdvice::MatchedBy matchedBy)
{
    switch (matchedBy) {
    case StickBackupAdvice::MatchedBy::None: return "none";
    case StickBackupAdvice::MatchedBy::Fingerprint: return "fingerprint";
    case StickBackupAdvice::MatchedBy::Identifier: return "identifier";
    case StickBackupAdvice::MatchedBy::Label: return "label";
    case StickBackupAdvice::MatchedBy::Newest: return "newest";
    }
    return "none";
}

}  // namespace seabass::application
