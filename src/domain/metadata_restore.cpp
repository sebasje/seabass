#include "domain/metadata_restore.hpp"

#include "domain/track_matching.hpp"

namespace seabass::domain
{

int MetadataRestoreProposal::cuesAdded() const
{
    if (!cuesOffered) {
        return 0;
    }
    const int after = static_cast<int>(cues.size());
    const int before = static_cast<int>(stickTrack.cues.size());
    return after > before ? after - before : 0;
}

std::vector<MetadataRestoreProposal> planMetadataRestore(const std::vector<Track> &stickTracks,
                                                          const std::vector<Track> &storedTracks,
                                                          MetadataRestorePolicy policy)
{
    const bool overwrite = policy == MetadataRestorePolicy::OverwriteConflicts;
    std::vector<MetadataRestoreProposal> proposals;

    for (const auto &[stick, stored] : matchTracks(stickTracks, storedTracks)) {
        MetadataRestoreProposal proposal;
        proposal.stickTrack = *stick;
        proposal.storedId = stored->sourceId;

        // ---- cues ----
        if (!stored->cues.empty()) {
            if (stick->cues.empty()) {
                proposal.cuesOffered = true;
                proposal.cuesFillAGap = true;
                proposal.cues = stored->cues;
            } else if (!cueSetsEqual(stick->cues, stored->cues)) {
                proposal.cuesConflict = true;
                if (overwrite) {
                    proposal.cuesOffered = true;
                    proposal.cues = stored->cues;
                }
            }
        }

        // ---- rating ----
        if (stored->rating) {
            if (!stick->rating) {
                proposal.ratingOffered = true;
                proposal.rating = stored->rating;
            } else if (*stick->rating != *stored->rating) {
                proposal.ratingConflict = true;
                if (overwrite) {
                    proposal.ratingOffered = true;
                    proposal.rating = stored->rating;
                }
            }
        }

        // ---- comment ----
        if (!stored->comment.empty()) {
            if (stick->comment.empty()) {
                proposal.commentOffered = true;
                proposal.comment = stored->comment;
            } else if (stick->comment != stored->comment) {
                proposal.commentConflict = true;
                if (overwrite) {
                    proposal.commentOffered = true;
                    proposal.comment = stored->comment;
                }
            }
        }

        if (proposal.offersAnything()) {
            proposals.push_back(std::move(proposal));
        }
    }
    return proposals;
}

}  // namespace seabass::domain
