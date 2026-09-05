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

- **Format USB Stick** (added 2026-09-05) — erases and reformats a
  removable drive as FAT32 or exFAT (always MBR), for a stick that's
  never been prepared for CDJ/XDJ/Engine OS hardware, or one being
  reused. The only feature so far that can destroy an entire drive
  outright, not just modify or consolidate library data on one: layered
  with its own extra safety net beyond the experimental gate itself (a
  must-dismiss warning popup when the selected drive already has a
  recognized DJ library, a red "Data will be lost" badge for any
  non-blank drive, no drive preselected by default, and a final confirm
  dialog naming the exact drive/path with a type-to-confirm gate). The
  actual destructive call (udisks2's CreatePartitionAndFormat on Linux,
  PowerShell's Format-Volume on Windows) is unverified against real
  hardware on either platform: `linux_usb_formatter.cpp` flags this in
  its own comment, and there's no Windows machine in this dev
  environment to exercise `windows_usb_formatter.cpp` at all. Promote to
  stable once both platforms have been confirmed against real hardware.

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
