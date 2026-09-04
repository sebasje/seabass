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
| `library_consistency_controller.cpp` | 3 |
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
`SyncPage.qml`); Clean Up doesn't yet.

Once (1) is done for `cleanup_controller.cpp`, adding a scope is small:

- Give `CleanupController`'s entry point(s) the same `playlistName`
  parameter `SyncController::analyze()` has, and filter each scanned
  catalog's tracks through `domain::filterByScope(tracks,
  domain::TrackScope::playlist(name))` before whatever Clean Up does with
  them — same seam, same pattern.
- Reuse `PlaylistPickerCombo.qml` (`src/gui/qml/PlaylistPickerCombo.qml`)
  for the picker UI — it's already generic (`model` + `currentIndex` +
  `playlistPicked` signal), no Sync-specific assumptions baked in.
- `CleanupController` will need its own `playlistNames`/
  `playlistTrackCounts` properties, built the same way
  `SyncController`'s are (`collectPlaylistSummary()` in
  `sync_controller.cpp` — worth promoting to a shared free function once a
  second caller needs the exact same union-across-catalogs logic, rather
  than copy-pasting it a second time).

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
