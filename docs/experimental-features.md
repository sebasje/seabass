# Experimental features

Features gated behind `AppSettingsController::experimentalFeaturesEnabled`
(Settings → Experimental features), off by default. See that property's own
doc comment and `ActionCard.qml`'s `experimental` property for how the gate
works, and the `SEABASS_EXPERIMENTAL` CMake option for the build-time
opt-in-mechanism switch.

New non-trivial features default to this list. Move an entry to "Graduated
to stable" (and drop `experimental: true` from its `ActionCard`) once it's
seen real, successful use — most importantly, an actual write/apply path
exercised live against real hardware, not just a read-only scan.

## Currently experimental

- **Full Stick Backup and Restore** (added 2026-09-05) — backs a whole
  stick up into one browsable `.zip` on this computer
  (`~/Seabass Backups/<label>.zip`, changeable in App Settings) and keeps
  it current incrementally; restores onto the same stick or a fresh one.
  Three gated surfaces: the "Full Stick Backup" `ActionCard` on
  `BackupsHubPage.qml`, the "Restore a Stick Backup" tool button on
  `StickListPage.qml`'s header (top-level, like Format USB Stick, because
  the target is often a blank replacement drive), and the backup-folder
  section on `AppSettingsPage.qml`. Design and every decision behind it:
  `docs/stick-backup-plan.md`. Experimental because it introduces a new
  on-disk format (ZIP64/STORE with a manifest and a crash-safe append-only
  update protocol) and a restore path that overwrites files on a stick.
  The fault-injection suite is green, but graduation needs real use:
  several incremental backups of a real stick over weeks, a cancel/keep/
  resume cycle, a compaction, and at least one restore onto a fresh
  exFAT stick that Engine DJ / a player then reads without complaint.

- **Matching** (added 2026-09-04) — a panel on the Library page
  (`MatchingPage.qml`) that finds tracks compatible in key (Camelot-wheel
  Harmonic/Nearby matching, or Ignore Key) and BPM with whichever Browse row you've
  marked as the one you're editing, to help build out a playlist around
  it. Unlike every other entry on this list, gating isn't just
  `ActionCard`'s `experimental` property: the whole panel (plus the
  playlist drawer it moves the old always-on playlist pane into) is
  visible only when `experimentalFeaturesEnabled` is on
  (`ScanPage.qml`'s `matchingEnabled`), and it carries its own extra
  `PREVIEW` badge on top of that, because the search/filter side is real
  but the write side isn't: no format (rekordbox, Engine, OneLibrary) has
  a playlist-mutation writer yet, so Before/After and the row reorder
  arrows just report a "preview, not saved" status instead of touching
  anything on disk. The Genre filter is present but disabled for the same
  reason one level down — `domain::Track` has no genre field at all yet.
  Promote to stable once a real per-format playlist writer exists and
  Before/After actually writes.

- **Create Engine Library** (added 2026-08-30) — builds a brand-new
  Engine Library database from scratch out of an existing rekordbox
  export, for a stick/SD card that has never been prepared for Engine OS
  hardware. The first feature in this app that fabricates an entire new
  database rather than modifying one Engine itself already created.
  Deliberately narrow: title/artist/BPM/key/duration/bitrate/rating/
  comment/hot+memory cues plus a simple two-point approximate beatgrid;
  no cover art (libdjinterop's own album_art API is unfinished), no real
  per-beat grid, no waveform, no playlists. Verified by creating a
  library and reading it back correctly with this app's own reader
  (`libdjinterop_engine_library_creator_test`); never tested against
  real Denon hardware. Exposes the Engine schema generation (1.x/2.x/3.x)
  as a user choice specifically because real firmware compatibility per
  generation is unverified. Promote to stable once Sebas has confirmed a
  created library works correctly on real Denon hardware (he has a Prime
  GO+ to test against).

## Graduated to stable

- **Library Health** (added 2026-08-29, graduated 2026-08-30) —
  cross-catalog consistency scan/repair, plus the 0:00-junk-memory-cue
  cleanup.

- **Stick Statistics** (added 2026-08-29, graduated 2026-08-30) —
  filesystem/hardware info, per-catalog library stats, a Filelight-style
  disk usage breakdown, and a local read-speed benchmark with history.
