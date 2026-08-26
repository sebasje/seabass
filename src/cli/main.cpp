#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/library_reader.hpp"
#include "application/ports/removable_media_locator.hpp"
#include "application/use_cases/consolidate_duplicate_cues.hpp"
#include "application/use_cases/scan_library.hpp"
#include "cli/console.hpp"
#include "cli/terminal_progress_reporter.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/media/linux_removable_media_locator.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

using djconvert::application::BackupStore;
using djconvert::application::ConsolidateDuplicateCues;
using djconvert::application::CueWriter;
using djconvert::application::DetectedStick;
using djconvert::application::LibraryReader;
using djconvert::application::RemovableMediaLocator;
using djconvert::application::ScanLibrary;
using djconvert::cli::Color;
using djconvert::cli::Console;
using djconvert::domain::ConsolidationPlan;
using djconvert::domain::Track;

namespace fs = std::filesystem;

namespace
{

constexpr size_t DefaultKeepBackups = 10;

void printUsage()
{
    Console::heading("djconvert -- sync rekordbox and Denon Engine DJ libraries");
    Console::info("");
    Console::info("djconvert reads DJ track libraries off USB sticks prepared by rekordbox or");
    Console::info("Engine DJ (the format Denon's Prime-series gear uses), so cue points can");
    Console::info("eventually be kept in sync between the two. Right now, scanning (read-only");
    Console::info("reporting) and duplicate-track cue consolidation are implemented; syncing");
    Console::info("between the two formats is still in development.");
    Console::info("");
    Console::heading("Usage");
    Console::info("  djconvert scan [--rekordbox [PATH]] [--engine [PATH]] [--verbose] [--auto]");
    Console::info("  djconvert backups [--rekordbox [PATH]] [--engine [PATH]] [--clean] [--keep N]");
    Console::info("  djconvert --help");
    Console::info("");
    Console::heading("Commands");
    Console::info("  scan     Reports every track and cue point found in a library, then checks");
    Console::info("           for duplicate copies of the same track with inconsistent cues (see");
    Console::info("           \"Duplicate-track cue consolidation\" below). Scanning itself never");
    Console::info("           modifies anything; consolidating duplicates does write, but only");
    Console::info("           after backing up the file(s) it's about to change, and only with");
    Console::info("           your confirmation unless --auto is given.");
    Console::info("  backups  Lists the backups djconvert has made on a stick (see \"Backups\"");
    Console::info("           below). With --clean, deletes the oldest ones so at most --keep");
    Console::info("           N remain (default " + std::to_string(DefaultKeepBackups) +
                   "), freeing space on the stick.");
    Console::info("");
    Console::heading("Options");
    Console::info("  --rekordbox [PATH]  Scan a rekordbox USB export. PATH is the folder that");
    Console::info("                      contains \"rekordbox/\" and \"USBANLZ/\" (i.e. the");
    Console::info("                      \"PIONEER\" folder itself). If PATH is omitted, an");
    Console::info("                      inserted USB stick is auto-detected.");
    Console::info("  --engine [PATH]     Scan an Engine Library. PATH is the folder that");
    Console::info("                      contains \"Database2/\" (i.e. the \"Engine Library\"");
    Console::info("                      folder itself). If PATH is omitted, an inserted USB");
    Console::info("                      stick is auto-detected.");
    Console::info("  --auto              Don't prompt before consolidating duplicate cues.");
    Console::info("                      Only ever applies when a group is unambiguous --");
    Console::info("                      exactly one copy has cues, the rest have none. A");
    Console::info("                      real conflict (copies with *different* cues) is never");
    Console::info("                      auto-resolved, --auto or not -- it's only reported.");
    Console::info("  --verbose           Print extra detail (resolved paths, per-item");
    Console::info("                      diagnostics). Default output is already");
    Console::info("                      informational, not silent -- this adds more on top.");
    Console::info("  --help, -h          Show this help and exit.");
    Console::info("");
    Console::heading("Auto-detection");
    Console::info("  Run \"djconvert scan\" with no --rekordbox/--engine PATH to auto-detect an");
    Console::info("  inserted USB stick. A single stick often carries both a \"PIONEER\" folder");
    Console::info("  and an \"Engine Library\" folder side by side -- if so, both are scanned");
    Console::info("  and reported. If more than one candidate stick is mounted, djconvert warns");
    Console::info("  and lists them instead of guessing which one you meant; re-run naming the");
    Console::info("  stick's path explicitly with --rekordbox/--engine.");
    Console::info("");
    Console::heading("Duplicate-track cue consolidation");
    Console::info("  A stick can hold the same track more than once (re-imports, duplicate");
    Console::info("  rows). If exactly one copy has hot cues and the others have none, djconvert");
    Console::info("  offers to copy those cues onto the cue-less copies -- this runs as the");
    Console::info("  last step of a scan, after all reporting is done. It's only implemented");
    Console::info("  for Engine libraries so far (rekordbox has no write path yet); rekordbox");
    Console::info("  duplicates are still detected and reported, just not auto-fixed. If");
    Console::info("  copies have cues that actually *differ*, that's reported as a conflict and");
    Console::info("  never touched -- you decide by hand which copy is right.");
    Console::info("");
    Console::heading("Backups");
    Console::info("  Every write djconvert makes (currently: duplicate-cue consolidation) backs");
    Console::info("  up the file(s) it's about to touch first, under");
    Console::info("  \"<stick root>/.djconvert-backups/<timestamp>-<label>/\" -- shared across");
    Console::info("  rekordbox and Engine, since both live under the same stick root. Nothing");
    Console::info("  is ever deleted automatically; run \"djconvert backups --clean\" yourself to");
    Console::info("  prune old ones once they've accumulated (default: keep the " +
                   std::to_string(DefaultKeepBackups) + " most recent).");
    Console::info("");
    Console::heading("Examples");
    Console::info("  djconvert scan                              # auto-detect an inserted stick");
    Console::info("  djconvert scan --rekordbox                  # auto-detect, rekordbox side only");
    Console::info("  djconvert scan --rekordbox /media/me/NEXUS  # scan a specific rekordbox stick");
    Console::info("  djconvert scan --engine /media/me/NEXUS --verbose");
    Console::info("  djconvert scan --engine --auto              # also auto-fix unambiguous duplicates");
    Console::info("  djconvert backups                           # list backups on an inserted stick");
    Console::info("  djconvert backups --clean --keep 3          # keep only the 3 most recent");
}

void printReport(const std::string &heading, const std::vector<Track> &tracks)
{
    int tracksWithCues = 0;
    int totalCues = 0;
    for (const auto &track : tracks) {
        if (!track.cues.empty()) {
            tracksWithCues++;
            totalCues += static_cast<int>(track.cues.size());
        }
    }

    Console::info("");
    Console::heading(heading);
    Console::info("  tracks:           " + std::to_string(tracks.size()));
    Console::info("  tracks with cues: " + std::to_string(tracksWithCues));
    Console::info("  total cues:       " + std::to_string(totalCues));

    int shown = 0;
    for (const auto &track : tracks) {
        if (track.cues.empty() || shown >= 5) {
            continue;
        }
        shown++;
        Console::info("");
        Console::info(Console::colorize(track.title, Color::Bold) + " (" + track.artist + ") [" +
                       std::to_string(static_cast<int>(track.durationSeconds)) + "s]");
        Console::verbose("  id=" + track.sourceId + " file=" + track.filename);
        for (const auto &cue : track.cues) {
            bool isHot = cue.kind == djconvert::domain::CuePoint::Kind::Hot;
            std::string line = std::string("  ") + (isHot ? "hot" : "mem") + " " + std::to_string(cue.hotCueNumber) +
                                " @ " + std::to_string(static_cast<long>(cue.positionMs)) + "ms";
            Console::info(Console::colorize(line, isHot ? Color::Cyan : Color::Gray) + " " + cue.color +
                           (cue.comment.empty() ? "" : (" \"" + cue.comment + "\"")));
        }
    }
}

std::vector<Track> scanPath(std::unique_ptr<LibraryReader> reader)
{
    djconvert::cli::TerminalProgressReporter progress;
    reader->setProgressReporter(progress);

    ScanLibrary useCase(*reader);
    return useCase.execute();
}

// Last step of a scan: find duplicate tracks and, where an unambiguous
// consolidation is possible and a CueWriter exists for this format, offer
// to apply it (backing up first). writer/backupStore may be null when the
// format has no write path yet (rekordbox) -- such duplicates are still
// detected and reported, just never modified.
void handleDuplicates(const std::string &formatName, const std::vector<Track> &tracks, CueWriter *writer,
                       BackupStore *backupStore, const std::vector<std::string> &filesToBackUp, bool autoMode)
{
    auto plans = ConsolidateDuplicateCues().execute(tracks);
    if (plans.empty()) {
        return;
    }

    bool printedHeading = false;
    bool backedUp = false;
    auto ensureHeading = [&]() {
        if (!printedHeading) {
            Console::info("");
            Console::heading(formatName + ": duplicate tracks");
            printedHeading = true;
        }
    };

    for (const auto &plan : plans) {
        if (plan.kind == ConsolidationPlan::Kind::NoCues || plan.kind == ConsolidationPlan::Kind::AlreadyConsistent) {
            continue;
        }

        ensureHeading();

        if (plan.kind == ConsolidationPlan::Kind::Conflict) {
            Console::warn("\"" + plan.group.tracks.front().filename +
                           "\" has duplicate copies with different cues -- not touching them:");
            for (const auto &t : plan.group.tracks) {
                Console::info("  id=" + t.sourceId + "  cues=" + std::to_string(t.cues.size()));
            }
            continue;
        }

        // Kind::Unambiguous
        Console::info("\"" + plan.source->filename + "\": " + std::to_string(plan.source->cues.size()) +
                       " cue(s) on one copy (id=" + plan.source->sourceId + "), missing on " +
                       std::to_string(plan.targets.size()) + " other copy/copies.");

        if (!writer) {
            Console::info("  " + formatName + " writing isn't supported yet -- cues not copied.");
            continue;
        }

        bool apply = autoMode || Console::confirm("  copy these cues onto the other copy/copies?");
        if (!apply) {
            Console::info("  skipped.");
            continue;
        }

        if (!backedUp && backupStore) {
            auto record = backupStore->backup(filesToBackUp, "duplicate-cue-consolidation");
            Console::info("  backed up to " + record.path);
            backedUp = true;
        }

        for (const auto &target : plan.targets) {
            writer->writeHotCues(target.sourceId, plan.source->cues);
            Console::info("  copied onto id=" + target.sourceId);
        }
    }
}

// Resolves the path to scan for one format: the explicit path if the user
// gave one, otherwise the unique auto-detected stick carrying that format.
// Returns an empty optional (after printing an explanatory message) when
// resolution isn't possible -- no path found, or more than one candidate.
std::optional<std::string> resolvePath(const std::optional<std::string> &explicitPath,
                                        const std::vector<DetectedStick> &detected,
                                        const std::string &formatName,
                                        std::optional<std::string> DetectedStick::*member)
{
    if (explicitPath) {
        return explicitPath;
    }

    std::vector<const DetectedStick *> candidates;
    for (const auto &stick : detected) {
        if (stick.*member) {
            candidates.push_back(&stick);
        }
    }

    if (candidates.empty()) {
        Console::error("no " + formatName + " USB stick found. Mount one, or pass a path explicitly.");
        return std::nullopt;
    }
    if (candidates.size() > 1) {
        Console::warn("multiple " + formatName + " USB sticks found -- specify one explicitly:");
        for (const auto *stick : candidates) {
            Console::info("  " + stick->label + "  (" + stick->mountPoint + ")");
        }
        return std::nullopt;
    }

    Console::verbose("auto-detected " + formatName + " stick: " + candidates[0]->label);
    return *(candidates[0]->*member);
}

struct ResolvedLibraryPaths
{
    std::optional<std::string> rekordboxPath;
    std::optional<std::string> enginePath;
    bool ok = true;
};

// Shared by "scan" and "backups": resolves which format(s) to act on and
// their paths, auto-detecting a USB stick where a PATH wasn't given.
// wantRekordbox/wantEngine are updated in place when neither was requested
// (a bare invocation infers both from whatever the detected stick carries).
ResolvedLibraryPaths resolveLibraryPaths(bool &wantRekordbox, bool &wantEngine,
                                          const std::optional<std::string> &rekordboxPathArg,
                                          const std::optional<std::string> &enginePathArg)
{
    djconvert::infrastructure::media::LinuxRemovableMediaLocator locator;
    bool needsDetection = (wantRekordbox && !rekordboxPathArg) || (wantEngine && !enginePathArg) ||
                          (!wantRekordbox && !wantEngine);
    std::vector<DetectedStick> detected;
    if (needsDetection) {
        detected = locator.detect();
    }

    if (!wantRekordbox && !wantEngine) {
        if (detected.empty()) {
            Console::error("no rekordbox or Engine USB stick found. Mount one, or pass a path explicitly.");
            return {.ok = false};
        }
        if (detected.size() > 1) {
            Console::warn("multiple USB sticks found -- specify one explicitly with --rekordbox/--engine:");
            for (const auto &stick : detected) {
                std::string formats = (stick.rekordboxPath ? "rekordbox" : "");
                if (stick.enginePath) {
                    formats += (formats.empty() ? "" : "+") + std::string("engine");
                }
                Console::info("  " + stick.label + "  (" + stick.mountPoint + ")  [" + formats + "]");
            }
            return {.ok = false};
        }
        wantRekordbox = static_cast<bool>(detected[0].rekordboxPath);
        wantEngine = static_cast<bool>(detected[0].enginePath);
    }

    ResolvedLibraryPaths result;
    if (wantRekordbox) {
        result.rekordboxPath = resolvePath(rekordboxPathArg, detected, "rekordbox", &DetectedStick::rekordboxPath);
        result.ok = result.ok && result.rekordboxPath.has_value();
    }
    if (wantEngine) {
        result.enginePath = resolvePath(enginePathArg, detected, "engine", &DetectedStick::enginePath);
        result.ok = result.ok && result.enginePath.has_value();
    }
    return result;
}

std::string humanSize(std::uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return buf;
}

// Backups live under <stick root>/.djconvert-backups, shared across formats
// (the rekordbox and Engine folders on one stick share the same parent).
std::string backupDirFor(const ResolvedLibraryPaths &resolved)
{
    fs::path anyPath = resolved.enginePath ? *resolved.enginePath : *resolved.rekordboxPath;
    return (anyPath.parent_path() / ".djconvert-backups").string();
}

int runBackupsCommand(bool wantRekordbox, bool wantEngine, const std::optional<std::string> &rekordboxPath,
                       const std::optional<std::string> &enginePath, bool clean, size_t keepCount)
{
    auto resolved = resolveLibraryPaths(wantRekordbox, wantEngine, rekordboxPath, enginePath);
    if (!resolved.rekordboxPath && !resolved.enginePath) {
        return 1;
    }

    djconvert::infrastructure::backup::FilesystemBackupStore store(backupDirFor(resolved));

    if (clean) {
        auto freed = store.prune(keepCount);
        Console::info("freed " + humanSize(freed) + " (kept up to " + std::to_string(keepCount) +
                       " most recent backup(s))");
        return resolved.ok ? 0 : 1;
    }

    auto records = store.list();
    if (records.empty()) {
        Console::info("no backups found under " + backupDirFor(resolved));
        return resolved.ok ? 0 : 1;
    }

    Console::heading("Backups (" + backupDirFor(resolved) + ")");
    std::uint64_t total = 0;
    for (const auto &record : records) {
        Console::info("  " + record.id + "  " + humanSize(record.sizeBytes));
        total += record.sizeBytes;
    }
    Console::info("  total: " + humanSize(total) + " across " + std::to_string(records.size()) + " backup(s)");
    Console::info("run with --clean [--keep N] to prune old backups (default keep " +
                   std::to_string(DefaultKeepBackups) + ")");
    return resolved.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);

    bool verbose = false;
    bool help = false;
    bool autoMode = false;
    bool clean = false;
    bool wantRekordbox = false;
    bool wantEngine = false;
    size_t keepCount = DefaultKeepBackups;
    std::optional<std::string> rekordboxPath;
    std::optional<std::string> enginePath;
    std::vector<std::string> commands;

    auto looksLikeFlag = [](const std::string &s) { return s.size() >= 2 && s[0] == '-' && s[1] == '-'; };

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];
        if (arg == "--help" || arg == "-h") {
            help = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--auto") {
            autoMode = true;
        } else if (arg == "--clean") {
            clean = true;
        } else if (arg == "--keep") {
            if (i + 1 >= args.size()) {
                Console::error("--keep requires a number");
                return 1;
            }
            keepCount = std::stoul(args[++i]);
        } else if (arg == "--rekordbox") {
            wantRekordbox = true;
            if (i + 1 < args.size() && !looksLikeFlag(args[i + 1])) {
                rekordboxPath = args[++i];
            }
        } else if (arg == "--engine") {
            wantEngine = true;
            if (i + 1 < args.size() && !looksLikeFlag(args[i + 1])) {
                enginePath = args[++i];
            }
        } else if (looksLikeFlag(arg)) {
            Console::error("unknown option: " + arg);
            printUsage();
            return 1;
        } else {
            commands.push_back(arg);
        }
    }

    Console::setVerbose(verbose);

    if (help || commands.empty()) {
        printUsage();
        return help ? 0 : 1;
    }
    if (commands.size() != 1 || (commands[0] != "scan" && commands[0] != "backups")) {
        Console::error("unknown command: " + commands[0]);
        printUsage();
        return 1;
    }

    if (commands[0] == "backups") {
        return runBackupsCommand(wantRekordbox, wantEngine, rekordboxPath, enginePath, clean, keepCount);
    }

    // commands[0] == "scan". Paths that failed to resolve are simply absent
    // from `resolved` below (with the reason already printed) -- whatever
    // did resolve still gets scanned, matching this command's normal
    // "keep going, report the exit code" behavior.
    auto resolved = resolveLibraryPaths(wantRekordbox, wantEngine, rekordboxPath, enginePath);
    bool ok = resolved.ok;
    try {
        if (resolved.rekordboxPath) {
            auto tracks = scanPath(std::make_unique<djconvert::infrastructure::rekordbox::KaitaiRekordboxReader>(
                *resolved.rekordboxPath));
            printReport("rekordbox", tracks);
            // No CueWriter for rekordbox yet -- duplicates are detected and reported, never applied.
            handleDuplicates("rekordbox", tracks, nullptr, nullptr, {}, autoMode);
        }
        if (resolved.enginePath) {
            auto tracks = scanPath(
                std::make_unique<djconvert::infrastructure::engine::LibdjinteropEngineReader>(*resolved.enginePath));
            printReport("engine", tracks);

            djconvert::infrastructure::engine::LibdjinteropEngineCueWriter writer(*resolved.enginePath);
            djconvert::infrastructure::backup::FilesystemBackupStore backupStore(backupDirFor(resolved));
            std::vector<std::string> filesToBackUp = {
                (fs::path(*resolved.enginePath) / "Database2" / "m.db").string()};
            handleDuplicates("engine", tracks, &writer, &backupStore, filesToBackUp, autoMode);
        }
    } catch (const std::exception &e) {
        Console::error(e.what());
        return 1;
    }

    return ok ? 0 : 1;
}
