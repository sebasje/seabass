# Follow-ups: extending TrackScope / LibraryCatalogCache beyond Sync

## Why this exists

`domain::TrackScope` (`src/domain/track_scope.hpp`) and
`gui::LibraryCatalogCache` (`src/gui/library_catalog_cache.hpp`) were built
as generic, reusable pieces — not one-offs for Sync Cue Points — but this
first pass only wired them into **two** controllers
(`SyncController`, `StickStatisticsController`), deliberately. A repo-wide
grep at the time this was written found `ScanLibrary(reader).execute()` (a
full catalog read) called independently from **8 controllers, 26 call
sites total**:

| Controller | `ScanLibrary` call sites |
|---|---|
| `cleanup_controller.cpp` | 7 |
| `add_cue_controller.cpp` | 3 |
| `stick_statistics_controller.cpp` | 3 (done — see above) |
| `library_consistency_controller.cpp` | 3 (done — see below) |
| `local_cue_controller.cpp` | 3 |
| `duplicates_controller.cpp` | 3 |
| `sync_controller.cpp` | 3 (done — see above) |
| `engine_library_creator_controller.cpp` | 1 |

Migrating all of them in one pass wasn't attempted: this is a careful,
heavily-commented, "verify against real hardware" codebase (see
`README.md`'s own beta-software warning), not one to mechanically touch 8
controllers at once without individually verifying each against real
stick data. What follows is the concrete, mechanical work left, written up
so the pattern doesn't get lost.

## 1. Migrate the remaining 6 controllers to `LibraryCatalogCache`

For each: replace `infrastructure::rekordbox::KaitaiRekordboxReader
reader(path); application::ScanLibrary(reader).execute()` (and the
Engine/OneLibrary equivalents) with
`gui::LibraryCatalogCache::instance().tracksFor("rekordbox", path)` (etc.),
exactly the substitution already done in `SyncController::runAnalyzeTask`
and `StickStatisticsController::runScanTask` — see those two for the
pattern, including how to thread a `ProgressReporter` through on a caller
that wants one (`SyncController` does; `StickStatisticsController`
doesn't).

`cleanup_controller.cpp` is the biggest win here: 7 call sites, several of
which likely re-scan the *same* catalog more than once within a single
Clean Up run (e.g. once for orphan detection, once for duplicate
detection) — those collapse to one real read each, not just one across
controllers.

**Write paths must call `invalidate()`.** Any controller that writes to a
catalog (Clean Up's cue/file writes, Local Cue's restore, etc.) needs the
same `LibraryCatalogCache::instance().invalidate(format, path)` calls
`SyncController::onWriteFinished()` already makes, right after a
successful write and before anything re-scans. Skipping this means a
write's own effects wouldn't be visible until the next mtime-driven
staleness check happens to catch it.

## 2. Wire `TrackScope` into Clean Up (and friends) for playlist/selection scoping

This is the feature Sebas actually asked for when `TrackScope` was
designed: "make pretty much all cleanup and sync operations operate on a
subset of tracks... so we can clean up our library in small steps." Sync
Cue Points has it (a `Playlist:` picker in its header, see
`SyncPage.qml`).

**Done: Clean Up Stray Cues** (`library_consistency_controller.cpp` /
`JunkCuePage.qml`). `LibraryConsistencyController::scan()` now takes the
same `playlistName` parameter `SyncController::analyze()` has (empty =
whole library); `runScanTask()` tallies each format's own playlist
membership *before* scoping (so the picker's own choices never shrink),
then filters through `domain::filterByScope(tracks,
domain::TrackScope::playlist(name))` before junk-cue detection and the
consistency check both run on the (possibly scoped) result. Since this
controller's scan is progressive (one format at a time, see
`scanNextPendingFormat()`), the cross-catalog playlist-name union is
folded together incrementally as each format's scan completes
(`LibraryConsistencyController::mergePlaylistSummary()`) rather than
built in one pass the way `SyncController`'s own
`collectPlaylistSummary()` does — same max-count-per-name semantics,
different accumulation shape to fit the progressive scan. `scanTracks()`
was also migrated to `LibraryCatalogCache::instance().tracksFor()` as
part of this (a prerequisite for (1) below), and `onWriteFinished()` now
invalidates all three catalogs' cache entries before its post-write
re-scan, same convention as `SyncController::onWriteFinished()`.
`JunkCuePage.qml` reuses `PlaylistPickerCombo.qml` unmodified for the
picker UI.

**Still open: Clean Up Duplicates** (`cleanup_controller.cpp` /
`CleanupPage.qml`, a separate controller/page from the one above despite
the similar name — see `CleanupPage.qml`'s own `BackBreadcrumb` title
"Clean Up Duplicates"). Once (1) is done for `cleanup_controller.cpp`,
adding a scope here is the same shape as what Clean Up Stray Cues just
got:

- Give `CleanupController`'s entry point(s) the same `playlistName`
  parameter, and filter each scanned catalog's tracks through
  `domain::filterByScope(tracks, domain::TrackScope::playlist(name))`
  before whatever Clean Up Duplicates does with them — same seam, same
  pattern.
- Reuse `PlaylistPickerCombo.qml` for the picker UI — already generic
  (`model` + `currentIndex` + `playlistPicked` signal), no Sync-specific
  assumptions baked in.
- `CleanupController` will need its own `playlistNames`/
  `playlistTrackCounts` properties. Whether that's a one-pass tally (like
  `SyncController`'s `collectPlaylistSummary()`) or an incremental merge
  (like `LibraryConsistencyController`'s `mergePlaylistSummary()`, added
  above) depends on whether `CleanupController`'s own scan ends up
  progressive per-format or all-at-once — check that before copying
  either pattern verbatim.

## 3. `TrackScope::arbitrary()` + manual multi-select

`TrackScope` already has an `Arbitrary` kind (an explicit
`std::set<TrackId>`, i.e. `(format, sourceId)` pairs) for exactly this,
but nothing produces one yet — no checkbox/multi-select UI exists on any
track table in this app. Building that is real, separate UI work (a
selection-tracking mechanism threaded through whichever `TrackListModel`-
backed view, most likely `ScanPage.qml`'s own track table), out of scope
for this note beyond flagging that `TrackScope`'s own design already has
the seam ready for it.

## 4. `ScanController`/`ScanPage`'s own filtering, for large libraries

Separate from the above (this one's about performance on `ScanPage`'s own
already-existing search/playlist/sort UI, not adding scope to a new
feature): `ScanController::applyFilters()` still rebuilds and copies a
whole `std::vector<domain::Track>` on every filter/search/sort change,
then calls `TrackListModel::setTracks()`, which resets the entire Qt
model — full `beginResetModel()`/`endResetModel()`, discarding scroll
position and forcing QML to recreate every visible delegate. For a very
large library, on every keystroke in the search box, that's real
overhead.

The idiomatic fix is a `QSortFilterProxyModel` (or a custom subclass
overriding `filterAcceptsRow`/`lessThan`) sitting in front of
`TrackListModel`, with the *current* `TrackScope` (playlist/search) as
its filter predicate — filtering becomes incremental row add/remove
instead of copy-everything-and-reset. `TrackScope::matches()` is already
the right shape to drive `filterAcceptsRow`. Not attempted here: this
touches `ScanPage`'s own core browsing UI, the highest-traffic page in
the app, and deserves its own dedicated pass with real before/after
verification against a large library — not a incidental addition to a
Sync-focused change.
