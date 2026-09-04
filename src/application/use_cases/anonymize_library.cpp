#include "application/use_cases/anonymize_library.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "infrastructure/engine/libdjinterop_engine_anonymizer.hpp"
#include "infrastructure/rekordbox/rekordbox_library_anonymizer.hpp"
#include "infrastructure/zip_archive_writer.hpp"

namespace seabass::application
{

namespace fs = std::filesystem;

namespace
{

// No real version plumbing exists in this codebase yet (see
// BRAINSTORM.md's own separate "Add version, show it in --help and on
// About page" item) -- this is a stopgap literal, not a build-system
// integration, and should be replaced with the real thing once that
// item lands.
constexpr const char *SeabassVersionStopgap = "0.6 (beta)";

std::string hostOsName()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown OS";
#endif
}

std::uintmax_t directorySizeBytes(const fs::path &dir)
{
    std::uintmax_t total = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return 0;
    }
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            total += it->file_size(fileEc);
        }
    }
    return total;
}

// Blended, catalog-specific compression ratios measured with `gzip -9`
// against real rekordbox/Engine library files (see the plan this
// implements) -- an *estimate*, not a guarantee, of what a zip archive
// of the actual output would come to; printed alongside the real raw
// size so a caller (or the CLI's own printed summary) can judge whether
// to re-run with a smaller AnonymizationOptions::maxTracks before
// attaching this to an email.
std::uintmax_t estimateZippedBytes(std::uintmax_t rekordboxRawBytes, std::uintmax_t engineRawBytes)
{
    return static_cast<std::uintmax_t>(static_cast<double>(rekordboxRawBytes) * 0.47) +
           static_cast<std::uintmax_t>(static_cast<double>(engineRawBytes) * 0.58);
}

std::string humanSize(std::uintmax_t bytes)
{
    constexpr double Kib = 1024.0;
    constexpr double Mib = Kib * 1024.0;
    std::ostringstream oss;
    if (bytes >= static_cast<std::uintmax_t>(Mib)) {
        oss.precision(1);
        oss << std::fixed << (static_cast<double>(bytes) / Mib) << " MB";
    } else {
        oss.precision(1);
        oss << std::fixed << (static_cast<double>(bytes) / Kib) << " KB";
    }
    return oss.str();
}

void writeManifest(const fs::path &manifestPath, const AnonymizationSummary &summary,
                    const AnonymizationOptions &options)
{
    std::ostringstream m;
    m << "Seabass anonymized library export\n";
    m << "==================================\n\n";
    m << "Seabass version: " << SeabassVersionStopgap << "\n";
    m << "Host OS: " << hostOsName() << "\n";
    m << "Hardware (as entered by the submitter): " << (options.hardware.empty() ? "(none given)" : options.hardware)
      << "\n";
    m << "Notes (as entered by the submitter): " << (options.notes.empty() ? "(none given)" : options.notes) << "\n\n";

    if (summary.rekordboxAttempted) {
        m << "rekordbox:\n";
        if (summary.rekordboxError.empty()) {
            m << "Tracks kept: " << summary.rekordboxTracksKept << "\n";
            m << "Tracks dropped (--max-tracks): " << summary.rekordboxTracksDropped << "\n";
            m << "Distinct artists renamed: " << summary.rekordboxArtistsRenamed << "\n";
            m << "Playlists/folders renamed: " << summary.rekordboxPlaylistsRenamed << "\n";
        } else {
            m << "FAILED: " << summary.rekordboxError << "\n";
        }
        m << "\n";
    }
    if (summary.engineAttempted) {
        m << "Engine:\n";
        if (summary.engineError.empty()) {
            m << "Tracks kept: " << summary.engineTracksKept << "\n";
            m << "Tracks dropped (--max-tracks): " << summary.engineTracksDropped << "\n";
            m << "Playlists/folders renamed: " << summary.enginePlaylistsRenamed << "\n";
        } else {
            m << "FAILED: " << summary.engineError << "\n";
        }
        m << "\n";
    }

    m << "What's included vs. stripped, on every track:\n"
         "  KEPT as-is: format, file size, bitrate, duration, BPM, key,\n"
         "    hot/memory cue positions and colors, rating, play count,\n"
         "    last-played date, whether it's a streaming-service track,\n"
         "    playlist membership/position, the low-resolution monochrome\n"
         "    waveform preview actually used by this app, beatgrid.\n"
         "  REPLACED with placeholder text (e.g. \"Track 014\"): title,\n"
         "    artist, comment, cue comments, filename/file path,\n"
         "    playlist/folder names.\n"
         "  REMOVED entirely: artwork images, the detailed color and\n"
         "    scrolling waveform data rekordbox's own UI uses during\n"
         "    playback (not read by this app), original file paths.\n\n";

    m << "Output size: " << humanSize(summary.outputSizeBytes) << " raw, roughly "
      << humanSize(summary.estimatedZippedBytes) << " estimated once zipped.\n\n";

    m << "Nothing has been sent anywhere; this only wrote a single zip\n"
         "file. Review its contents, then attach that zip to an email to\n"
         "sebas@kde.org if you'd like to help test against your\n"
         "hardware/library. This dataset may be published as part of the\n"
         "project's test suite. If there's anything in the hardware or\n"
         "notes text above you'd rather not have published, leave it out\n"
         "here and mention it directly in your email instead. If this is\n"
         "too large to attach, re-run with --max-tracks to include a\n"
         "smaller sample.\n";

    std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
    out << m.str();
}

}  // namespace

bool AnonymizationSummary::succeeded() const
{
    bool anyAttempted = rekordboxAttempted || engineAttempted;
    bool anyFailed = (rekordboxAttempted && !rekordboxError.empty()) || (engineAttempted && !engineError.empty());
    return anyAttempted && !anyFailed;
}

AnonymizationSummary AnonymizeLibrary::execute(const std::optional<std::string> &rekordboxRoot,
                                                const std::optional<std::string> &engineRoot,
                                                const std::string &outputDir, const AnonymizationOptions &options,
                                                ProgressReporter &reporter)
{
    AnonymizationSummary summary;

    fs::create_directories(outputDir);

    if (rekordboxRoot) {
        summary.rekordboxAttempted = true;
        auto result = infrastructure::rekordbox::anonymizeRekordboxLibrary(
            *rekordboxRoot, (fs::path(outputDir) / "rekordbox").string(), options.maxTracks, reporter);
        summary.rekordboxTracksKept = result.tracksKept;
        summary.rekordboxTracksDropped = result.tracksDropped;
        summary.rekordboxArtistsRenamed = result.artistsRenamed;
        summary.rekordboxPlaylistsRenamed = result.playlistsRenamed;
        summary.rekordboxError = result.errorMessage;
    }

    if (engineRoot) {
        summary.engineAttempted = true;
        auto result = infrastructure::engine::anonymizeEngineLibrary(
            *engineRoot, (fs::path(outputDir) / "engine").string(), options.maxTracks, reporter);
        summary.engineTracksKept = result.tracksKept;
        summary.engineTracksDropped = result.tracksDropped;
        summary.enginePlaylistsRenamed = result.playlistsRenamed;
        summary.engineError = result.errorMessage;
    }

    std::uintmax_t rekordboxBytes = directorySizeBytes(fs::path(outputDir) / "rekordbox");
    std::uintmax_t engineBytes = directorySizeBytes(fs::path(outputDir) / "engine");
    summary.outputSizeBytes = rekordboxBytes + engineBytes;
    summary.estimatedZippedBytes = estimateZippedBytes(rekordboxBytes, engineBytes);

    summary.manifestPath = (fs::path(outputDir) / "MANIFEST.txt").string();
    writeManifest(summary.manifestPath, summary, options);

    // Captured before the staging directory is removed below -- once
    // execute() returns, MANIFEST.txt only exists inside outputZipPath,
    // not as a standalone file a caller could read back off disk.
    {
        std::ifstream manifestIn(summary.manifestPath, std::ios::binary);
        std::ostringstream manifestContent;
        manifestContent << manifestIn.rdbuf();
        summary.manifestText = manifestContent.str();
    }

    // outputDir was only ever a staging area -- the actual deliverable
    // is one zip file, not a directory tree the user has to remember to
    // zip themselves before emailing it. fs::path's own manipulation
    // (rather than raw string/separator surgery, which would need a
    // separate code path for Windows' wide path::value_type) keeps this
    // portable: a trailing separator makes filename() report empty, so
    // that case falls back to parent_path() first, then += appends
    // ".zip" without inserting a fresh separator.
    fs::path zipPath(outputDir);
    if (zipPath.filename().empty()) {
        zipPath = zipPath.parent_path();
    }
    zipPath += ".zip";
    infrastructure::writeZipArchive(outputDir, zipPath);
    summary.outputZipPath = zipPath.string();
    std::error_code sizeEc;
    summary.finalZipBytes = fs::file_size(zipPath, sizeEc);

    fs::remove_all(outputDir);

    return summary;
}

}  // namespace seabass::application
