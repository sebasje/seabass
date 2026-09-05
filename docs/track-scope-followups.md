# Follow-ups: extending TrackScope / LibraryCatalogCache beyond Sync

## Why this exists

`domain::TrackScope` (`src/domain/track_scope.hpp`) and
`gui::LibraryCatalogCache` (`src/gui/library_catalog_cache.hpp`) were built
as generic, reusable pieces — not one-offs for Sync Cue Points — but the
first pass only wired the cache into **two** controllers
(`SyncController`, `StickStatisticsController`), deliberately. A repo-wide
grep at the time this was written found `ScanLibrary(reader).execute()` (a
full catalog read) called independently from **8 controllers, 26 call
sites total** (the table below; `ScanController` itself was missed by that
grep and turned out to be a ninth, found later — see item 1's status).

Migrating all of them in one pass wasn't attempted: this is a careful,
heavily-commented, "verify against real hardware" codebase (see
`README.md`'s own beta-software warning), not one to mechanically touch 8
controllers at once without individually verifying each against real
stick data.

## 1. Migrate every controller to `LibraryCatalogCache` — done

| Controller | `ScanLibrary` call sites | Status |
|---|---|---|
| `cleanup_controller.cpp` | 7 | done |
| `add_cue_controller.cpp` | 3 | done |
| `stick_statistics_controller.cpp` | 3 | done (first pass) |
| `library_consistency_controller.cpp` | 3 | done |
| `local_cue_controller.cpp` | 3 | done |
| `duplicates_controller.cpp` | 3 | done |
| `sync_controller.cpp` | 3 | done (first pass) |
| `engine_library_creator_controller.cpp` | 1 | done |
| `scan_controller.cpp` (missed by the original grep — it's `ScanPage`'s own browse/format-toggle scan, not caught because the earlier search was scoped to a fixed controller list rather than a fresh repo-wide grep) | 1 (+1 sibling-rekordbox scan for Engine artwork borrowing) | done |

Every controller now goes through `gui::LibraryCatalogCache::instance()
.tracksFor(format, path, progress)` instead of constructing its own reader
+ `application::ScanLibrary`. Every write path that mutates a catalog
(Clean Up's apply/undo/pending-deletion, Add Cue, Library Health's
repair/junk-cue removal, Local Cue's restore/undo, Duplicates' apply/undo,
Sync's apply/undo) calls `LibraryCatalogCache::instance().invalidate(format,
path)` right after the write succeeds, including the handful of
rekordbox-primary writers that also best-effort mirror into OneLibrary's
`exportLibrary.db` when one exists alongside `export.pdb` (Clean Up, Add
Cue, Local Cue) — those invalidate `"onelibrary"` too, not just the
primary format. `application::ScanLibrary`/the per-format reader includes
were removed from every file above once nothing local called them anymore
(`git grep 'application::ScanLibrary'` now only matches
`library_catalog_cache.cpp` itself).

Switching the Library page's format toggle (Rekordbox/Engine/OneLibrary)
back and forth no longer re-reads a catalog from disk that another page
already scanned this session — the original complaint this whole effort
started from.

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
