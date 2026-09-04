#include "scan_controller.hpp"

#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

#include "application/use_cases/scan_library.hpp"
#include "domain/track_matching.hpp"
#include "domain/track_scope.hpp"
#include "gui/local_file_url.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

namespace seabass::gui
{

TrackListModel::TrackListModel(QObject *parent) : QAbstractListModel(parent) {}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_tracks.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_tracks.size()) {
        return {};
    }
    const auto &track = m_tracks[static_cast<size_t>(index.row())];
    switch (role) {
    case SourceIdRole:
        return QString::fromStdString(track.sourceId);
    case TitleRole:
        return QString::fromStdString(track.title);
    case ArtistRole:
        return QString::fromStdString(track.artist);
    case DurationSecondsRole:
        return track.durationSeconds;
    case CueCountRole:
        return static_cast<int>(track.cues.size());
    case PlayCountRole:
        return track.playCount ? *track.playCount : -1;
    case FilePathRole:
        return QString::fromStdString(track.filePath);
    case ArtworkPathRole:
        return toLocalFileUrl(track.artworkPath);
    case BpmRole:
        return track.bpm;
    case KeyRole:
        return QString::fromStdString(track.key);
    case CuesRole: {
        QVariantList cues;
        for (const auto &c : track.cues) {
            QVariantMap m;
            m["kind"] = c.kind == domain::CuePoint::Kind::Hot ? QStringLiteral("hot") : QStringLiteral("memory");
            m["hotCueNumber"] = c.hotCueNumber;
            m["positionMs"] = c.positionMs;
            m["isLoop"] = c.isLoop;
            m["loopEndMs"] = c.loopEndMs;
            m["color"] = QString::fromStdString(c.color);
            m["comment"] = QString::fromStdString(c.comment);
            cues << m;
        }
        return cues;
    }
    case PlaylistNamesRole: {
        QStringList names;
        for (const auto &p : track.playlists) {
            names << QString::fromStdString(p.name);
        }
        return names;
    }
    case StreamingSourceRole:
        return QString::fromStdString(track.streamingSource);
    default:
        return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {SourceIdRole, "sourceId"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {DurationSecondsRole, "durationSeconds"},
        {CueCountRole, "cueCount"},
        {PlayCountRole, "playCount"},
        {FilePathRole, "filePath"},
        {ArtworkPathRole, "artworkPath"},
        {BpmRole, "bpm"},
        {KeyRole, "key"},
        {CuesRole, "cues"},
        {PlaylistNamesRole, "playlistNames"},
        {StreamingSourceRole, "streamingSource"},
    };
}

void TrackListModel::setTracks(std::vector<domain::Track> tracks)
{
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

namespace
{

// Runs entirely on a background thread (see ScanController::scan()) - no
// access to the controller itself, so everything it needs travels in by
// value and its result travels back out as a plain struct.
ScanTaskResult runScanTask(QString format, QString path, QString siblingRekordboxPath,
                            std::shared_ptr<QtProgressReporter> reporter)
{
    ScanTaskResult result;
    try {
        std::vector<domain::Track> tracks;
        if (format == "rekordbox") {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            application::ScanLibrary useCase(reader);
            tracks = useCase.execute();
        } else if (format == "engine") {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            application::ScanLibrary useCase(reader);
            tracks = useCase.execute();

            if (!siblingRekordboxPath.isEmpty()) {
                try {
                    // This is a second full scan (to build the artwork
                    // lookup) that can take as long as the one above, give
                    // it the same reporter rather than let the bar sit at
                    // 100% while this runs silently in the background.
                    infrastructure::rekordbox::KaitaiRekordboxReader rbReader(siblingRekordboxPath.toStdString());
                    rbReader.setProgressReporter(*reporter);
                    application::ScanLibrary rbUseCase(rbReader);
                    auto rbTracks = rbUseCase.execute();

                    std::unordered_map<std::string, std::string> artworkByTitleArtist;
                    for (const auto &rbTrack : rbTracks) {
                        if (rbTrack.artworkPath.empty() || rbTrack.title.empty() || rbTrack.artist.empty()) {
                            continue;
                        }
                        artworkByTitleArtist[domain::normalizeFilename(rbTrack.title + "|" + rbTrack.artist)] =
                            rbTrack.artworkPath;
                    }

                    for (auto &track : tracks) {
                        if (!track.artworkPath.empty() || track.title.empty() || track.artist.empty()) {
                            continue;
                        }
                        auto it = artworkByTitleArtist.find(domain::normalizeFilename(track.title + "|" + track.artist));
                        if (it != artworkByTitleArtist.end()) {
                            track.artworkPath = it->second;
                        }
                    }
                } catch (const std::exception &) {
                    // Borrowing cover art from the sibling library is a
                    // nice-to-have, never let it break browsing Engine
                    // tracks on their own.
                }
            }
        } else {
            // format == "onelibrary": path is the same PIONEER root
            // rekordbox uses (exportLibrary.db lives alongside export.pdb
            // under it), not a separate stored path. OneLibrary is a
            // third view onto that same side of the stick, not an
            // independent catalog with its own DetectedStick field.
            infrastructure::onelibrary::OneLibraryReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            application::ScanLibrary useCase(reader);
            tracks = useCase.execute();
        }
        result.tracks = std::move(tracks);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

ScanController::ScanController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<ScanTaskResult>::finished, this, &ScanController::onScanFinished);
}

void ScanController::scan(const QString &format, const QString &path, const QString &siblingRekordboxPath)
{
    if (m_busy) {
        return;  // a scan is already running, never overlap two
    }
    setErrorMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    // The reporter is owned by the background task (via shared_ptr, kept
    // alive for exactly as long as the task runs), not by this controller.
    // If the user navigates away and this ScanController is destroyed
    // mid-scan, the task keeps running harmlessly in the background instead
    // of touching a dangling object. Signals are connected with `this` as
    // the context object, so Qt stops delivering them once we're gone.
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });

    m_watcher.setFuture(QtConcurrent::run(runScanTask, format, path, siblingRekordboxPath, reporter));
}

void ScanController::onScanFinished()
{
    ScanTaskResult result = m_watcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        setBusy(false);
        return;
    }

    std::set<std::string> uniquePlaylistNames;
    std::unordered_map<std::string, int> countByPlaylist;
    for (const auto &track : result.tracks) {
        for (const auto &playlist : track.playlists) {
            uniquePlaylistNames.insert(playlist.name);
            countByPlaylist[playlist.name]++;
        }
    }
    m_playlistNames.clear();
    m_playlistTrackCounts.clear();
    for (const auto &name : uniquePlaylistNames) {
        QString qName = QString::fromStdString(name);
        m_playlistNames << qName;
        m_playlistTrackCounts[qName] = countByPlaylist[name];
    }

    // m_allTracks (which totalTrackCount() reads) must be updated before
    // playlistNamesChanged fires. QML bindings that read totalTrackCount
    // in response to that signal would otherwise see the previous scan's
    // track count for one notification cycle.
    m_allTracks = std::move(result.tracks);
    emit playlistNamesChanged();

    // Playlist selection is catalog-specific (a name picked in one
    // format's playlist list may not exist, or mean the same thing, in
    // another), so that's cleared on every fresh scan. The search query
    // is not: it's just free text, and the search field's own displayed
    // text doesn't get cleared alongside it (there's no reverse binding
    // from m_currentSearchQuery back to the QML field), so clearing it
    // here used to leave the field showing a query no longer actually
    // applied. Keeping it applied here instead means the field's text
    // and the actually-filtered results never drift apart.
    m_currentPlaylistFilter.clear();
    applyFilters();

    setBusy(false);
}

void ScanController::filterByPlaylist(const QString &playlistName)
{
    m_currentPlaylistFilter = playlistName;
    applyFilters();
}

void ScanController::search(const QString &query)
{
    m_currentSearchQuery = query;
    applyFilters();
}

bool ScanController::hasOneLibrary(const QString &pioneerRoot) const
{
    return infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot.toStdString());
}

void ScanController::setSort(const QString &field, bool ascending)
{
    m_sortField = field;
    m_sortAscending = ascending;
    applyFilters();
}

void ScanController::setHideStreamingTracks(bool hide)
{
    if (m_hideStreamingTracks == hide) {
        return;
    }
    m_hideStreamingTracks = hide;
    applyFilters();
}

QVariantList ScanController::findMergeCandidates(const QString &query, const QString &excludeSourceId) const
{
    QVariantList result;
    if (query.isEmpty()) {
        return result;
    }
    QString lowerQuery = query.toLower();
    std::string exclude = excludeSourceId.toStdString();
    for (const auto &track : m_allTracks) {
        if (track.sourceId == exclude) {
            continue;
        }
        // Streaming tracks (Engine/TIDAL) have no real local file.
        // Never suggest merging with one. See
        // domain::Track::streamingSource's own doc comment.
        if (!track.streamingSource.empty()) {
            continue;
        }
        QString title = QString::fromStdString(track.title);
        QString artist = QString::fromStdString(track.artist);
        if (!title.toLower().contains(lowerQuery) && !artist.toLower().contains(lowerQuery)) {
            continue;
        }
        QVariantMap m;
        m["sourceId"] = QString::fromStdString(track.sourceId);
        m["title"] = title;
        m["artist"] = artist;
        m["durationSeconds"] = track.durationSeconds;
        m["filePath"] = QString::fromStdString(track.filePath);
        result << m;
        if (result.size() >= 50) {
            break;
        }
    }
    return result;
}

namespace
{

std::optional<int> playlistPosition(const domain::Track &track, const std::string &playlistName)
{
    for (const auto &p : track.playlists) {
        if (p.name == playlistName) {
            return p.position;
        }
    }
    return std::nullopt;
}

// Camelot wheel position (1..12) plus minor/major, for sorting track.key
// harmonically instead of alphabetically -- a plain string compare puts
// "10A" before "2A" (wrong: '1' < '2'), and scrambles traditional-notation
// keys ("Am", "C", "F#m", ...) into note-letter order, which has nothing
// to do with which keys actually mix well together. Mirrors Theme.qml's
// parseCamelotKey() (kept in sync by hand -- QML can't be shared with C++
// here), including accepting either notation track.key may already hold:
// traditional ("Fm", engine's own "F♯m") or, on some real rekordbox
// libraries, Camelot notation stored as the key name itself ("8A").
std::optional<std::pair<int, bool>> camelotSortKey(const std::string &key)
{
    if (key.empty()) {
        return std::nullopt;
    }
    QString normalized = QString::fromStdString(key).replace(QChar(0x266F), '#').replace(QChar(0x266D), 'b');

    static const QRegularExpression camelotPattern(QStringLiteral("^(1[0-2]|[1-9])([AB])$"));
    auto camelotMatch = camelotPattern.match(normalized);
    if (camelotMatch.hasMatch()) {
        int camelotNumber = camelotMatch.captured(1).toInt();
        bool isMinor = camelotMatch.captured(2) == QStringLiteral("A");
        return std::make_pair(camelotNumber, isMinor);
    }

    static const std::unordered_map<std::string, int> pitchClassByName = {
        {"C", 0}, {"B#", 0}, {"C#", 1}, {"Db", 1}, {"D", 2}, {"D#", 3}, {"Eb", 3}, {"E", 4}, {"Fb", 4},
        {"F", 5}, {"E#", 5}, {"F#", 6}, {"Gb", 6}, {"G", 7}, {"G#", 8}, {"Ab", 8}, {"A", 9},
        {"A#", 10}, {"Bb", 10}, {"B", 11}, {"Cb", 11},
    };
    // Pitch class -> Camelot number, one table per mode (see Theme.qml's
    // own comment: derived from the wheel's relative-major/minor pairing).
    static const std::array<int, 12> camelotMinorByPitchClass = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
    static const std::array<int, 12> camelotMajorByPitchClass = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};

    std::string normalizedStd = normalized.toStdString();
    bool isMinor = normalizedStd.size() > 1 && normalizedStd.back() == 'm';
    std::string notePart = isMinor ? normalizedStd.substr(0, normalizedStd.size() - 1) : normalizedStd;
    auto pitchClassIt = pitchClassByName.find(notePart);
    if (pitchClassIt == pitchClassByName.end()) {
        return std::nullopt;
    }
    int camelotNumber = isMinor ? camelotMinorByPitchClass[static_cast<size_t>(pitchClassIt->second)]
                                 : camelotMajorByPitchClass[static_cast<size_t>(pitchClassIt->second)];
    return std::make_pair(camelotNumber, isMinor);
}

}  // namespace

void ScanController::applyFilters()
{
    std::vector<domain::Track> result = m_allTracks;
    std::string playlistFilter = m_currentPlaylistFilter.toStdString();

    if (!m_currentPlaylistFilter.isEmpty()) {
        result = domain::filterByScope(result, domain::TrackScope::playlist(playlistFilter));
    }

    if (m_hideStreamingTracks) {
        std::vector<domain::Track> filtered;
        for (const auto &track : result) {
            if (track.streamingSource.empty()) {
                filtered.push_back(track);
            }
        }
        result = std::move(filtered);
    }

    if (!m_currentSearchQuery.isEmpty()) {
        // Deliberately QString::toLower(), not domain::TrackScope::search()
        // -- TrackScope's search is ASCII-only case folding (see its own
        // doc comment), while this box has to handle real music metadata
        // correctly (accented artist/title names are common), which needs
        // QString's Unicode-aware case folding. Swapping this to
        // TrackScope would silently regress search for exactly the
        // tracks this codebase already goes out of its way to handle
        // correctly elsewhere (see KeyBadge.qml's own Unicode handling).
        QString query = m_currentSearchQuery.toLower();
        std::vector<domain::Track> filtered;
        for (const auto &track : result) {
            QString title = QString::fromStdString(track.title).toLower();
            QString artist = QString::fromStdString(track.artist).toLower();
            if (title.contains(query) || artist.contains(query)) {
                filtered.push_back(track);
            }
        }
        result = std::move(filtered);
    }

    // A strict weak ordering must return false for both (a, b) and (b, a)
    // when the two compare equal. Negating "a < b" to get descending order
    // breaks that (both (a, b) and (b, a) would return true for ties), so
    // descending order is done by swapping the arguments instead.
    auto lessAscending = [&](const domain::Track &a, const domain::Track &b) {
        if (m_sortField == "title") {
            return a.title < b.title;
        }
        if (m_sortField == "artist") {
            return a.artist < b.artist;
        }
        if (m_sortField == "key") {
            // Unparseable/missing keys sort as the lowest wheel position
            // (mirrors "plays"'s value_or(-1) sentinel below), so they
            // consistently land at one end rather than being scattered
            // alphabetically as plain strings would.
            auto keyA = camelotSortKey(a.key).value_or(std::make_pair(0, false));
            auto keyB = camelotSortKey(b.key).value_or(std::make_pair(0, false));
            return keyA < keyB;
        }
        if (m_sortField == "bpm") {
            return a.bpm < b.bpm;
        }
        if (m_sortField == "duration") {
            return a.durationSeconds < b.durationSeconds;
        }
        if (m_sortField == "cues") {
            return a.cues.size() < b.cues.size();
        }
        if (m_sortField == "plays") {
            return a.playCount.value_or(-1) < b.playCount.value_or(-1);
        }
        // "playlist", the selected playlist's own track order; when no
        // playlist is selected this leaves every position unknown, so
        // stable_sort just preserves scan order.
        auto posA = playlistPosition(a, playlistFilter);
        auto posB = playlistPosition(b, playlistFilter);
        return posA.value_or(std::numeric_limits<int>::max()) < posB.value_or(std::numeric_limits<int>::max());
    };
    std::stable_sort(result.begin(), result.end(), [&](const domain::Track &a, const domain::Track &b) {
        return m_sortAscending ? lessAscending(a, b) : lessAscending(b, a);
    });

    m_model.setTracks(std::move(result));
}

void ScanController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void ScanController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void ScanController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

}  // namespace seabass::gui
