#include "scan_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

#include "application/use_cases/scan_library.hpp"
#include "domain/track_matching.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

namespace djconvert::gui
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
        return QString::fromStdString(track.artworkPath);
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
            m["color"] = QString::fromStdString(c.color);
            cues << m;
        }
        return cues;
    }
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

// Runs entirely on a background thread (see ScanController::scan()) -- no
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
                    // lookup) that can take as long as the one above -- give
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
                    // nice-to-have -- never let it break browsing Engine
                    // tracks on their own.
                }
            }
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
        return;  // a scan is already running -- never overlap two
    }
    setErrorMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    // The reporter is owned by the background task (via shared_ptr, kept
    // alive for exactly as long as the task runs), not by this controller --
    // if the user navigates away and this ScanController is destroyed
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
    // playlistNamesChanged fires -- QML bindings that read totalTrackCount
    // in response to that signal would otherwise see the previous scan's
    // track count for one notification cycle.
    m_allTracks = std::move(result.tracks);
    emit playlistNamesChanged();

    m_currentPlaylistFilter.clear();
    m_currentSearchQuery.clear();
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

void ScanController::setSort(const QString &field, bool ascending)
{
    m_sortField = field;
    m_sortAscending = ascending;
    applyFilters();
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

}  // namespace

void ScanController::applyFilters()
{
    std::vector<domain::Track> result = m_allTracks;

    std::string playlistFilter = m_currentPlaylistFilter.toStdString();
    if (!m_currentPlaylistFilter.isEmpty()) {
        std::vector<domain::Track> filtered;
        for (const auto &track : result) {
            if (playlistPosition(track, playlistFilter)) {
                filtered.push_back(track);
            }
        }
        result = std::move(filtered);
    }

    if (!m_currentSearchQuery.isEmpty()) {
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
            return a.key < b.key;
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
        // "playlist" -- the selected playlist's own track order; when no
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

}  // namespace djconvert::gui
