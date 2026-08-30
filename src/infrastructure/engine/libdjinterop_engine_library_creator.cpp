#include "infrastructure/engine/libdjinterop_engine_library_creator.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include <djinterop/djinterop.hpp>
#include <djinterop/engine/engine.hpp>

#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/rekordbox_key_parser.hpp"

namespace djconvert::infrastructure::engine
{

namespace fs = std::filesystem;

namespace
{

djinterop::engine::engine_schema schemaFor(EngineSchemaGeneration generation)
{
    switch (generation) {
    case EngineSchemaGeneration::V1: return djinterop::engine::latest_v1_schema;
    case EngineSchemaGeneration::V2: return djinterop::engine::latest_v2_schema;
    case EngineSchemaGeneration::V3: return djinterop::engine::latest_v3_schema;
    }
    return djinterop::engine::latest_v2_schema;
}

// A default assumed when a track's own sample rate isn't known -- same
// fallback convention (and same reasoning) as
// LibdjinteropEngineCueWriter's own sample-rate handling.
constexpr double DefaultSampleRate = 44100.0;

}  // namespace

EngineLibraryCreationResult EngineLibraryCreator::create(const std::string &directory,
                                                           const std::vector<domain::Track> &tracks,
                                                           EngineSchemaGeneration schemaGeneration)
{
    EngineLibraryCreationResult result;

    if (djinterop::engine::database_exists(directory)) {
        result.errorMessage = "An Engine Library already exists at " + directory + " -- refusing to overwrite it.";
        return result;
    }

    try {
        auto db = djinterop::engine::create_database(directory, schemaFor(schemaGeneration));
        LibdjinteropEngineCueWriter cueWriter(directory);

        for (const auto &track : tracks) {
            // Streaming tracks have no local file by design (see
            // Track::streamingSource's own doc comment) and rows with no
            // resolved file path can't be referenced from a fresh
            // library at all -- both are skipped, not fabricated.
            if (!track.streamingSource.empty() || track.filePath.empty()) {
                result.tracksSkipped++;
                continue;
            }

            djinterop::track_snapshot snapshot;
            snapshot.title = track.title.empty() ? std::nullopt : std::optional(track.title);
            snapshot.artist = track.artist.empty() ? std::nullopt : std::optional(track.artist);
            if (track.bpm > 0.0) {
                snapshot.bpm = track.bpm;
            }
            if (auto key = parseRekordboxKey(track.key)) {
                snapshot.key = *key;
            }
            if (track.durationSeconds > 0.0) {
                snapshot.duration = std::chrono::milliseconds(static_cast<int64_t>(track.durationSeconds * 1000.0));
            }
            if (track.bitrate > 0) {
                snapshot.bitrate = track.bitrate;
            }
            if (track.rating.has_value()) {
                // Track::rating is 0-5 stars; Engine's own scale is 0-100.
                snapshot.rating = *track.rating * 20;
            }
            if (!track.comment.empty()) {
                snapshot.comment = track.comment;
            }
            if (track.fileSizeBytes > 0) {
                snapshot.file_bytes = track.fileSizeBytes;
            }

            std::error_code relError;
            fs::path relative = fs::relative(track.filePath, directory, relError);
            snapshot.relative_path = relError ? track.filePath : relative.generic_string();

            // Simple, approximate two-point beatgrid: assumes the track
            // starts exactly on a downbeat at sample 0, then a second
            // marker far enough out to cover the whole track at a
            // constant BPM. Not a substitute for a real per-beat grid --
            // see this class's own doc comment for why that's out of
            // scope for now -- but gives Engine something to quantize/
            // sync against rather than nothing at all.
            if (track.bpm > 0.0 && track.durationSeconds > 0.0) {
                double secondsPerBeat = 60.0 / track.bpm;
                double totalBeats = track.durationSeconds / secondsPerBeat;
                int64_t sampleCount = static_cast<int64_t>(track.durationSeconds * DefaultSampleRate);
                std::vector<djinterop::beatgrid_marker> grid = {
                    {0, 0.0},
                    {static_cast<int>(totalBeats), totalBeats * secondsPerBeat * DefaultSampleRate},
                };
                snapshot.beatgrid = djinterop::engine::normalize_beatgrid(grid, sampleCount);
                snapshot.sample_rate = DefaultSampleRate;
                snapshot.sample_count = static_cast<unsigned long long>(sampleCount);
            }

            djinterop::track created = db.create_track(snapshot);
            result.tracksCreated++;

            if (!track.cues.empty()) {
                cueWriter.writeHotCues(std::to_string(created.id()), track.cues);
                result.cuesCopied += static_cast<int>(track.cues.size());
            }
        }
    } catch (const std::exception &e) {
        result.errorMessage = e.what();
    }

    return result;
}

}  // namespace djconvert::infrastructure::engine
