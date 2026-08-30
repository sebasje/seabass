# Experimental features

Features gated behind `AppSettingsController::experimentalFeaturesEnabled`
(Settings → Experimental features), off by default. See that property's own
doc comment and `ActionCard.qml`'s `experimental` property for how the gate
works, and the `DJCONVERT_EXPERIMENTAL` CMake option for the build-time
opt-in-mechanism switch.

New non-trivial features default to this list. Move an entry to "Graduated
to stable" (and drop `experimental: true` from its `ActionCard`) once it's
seen real, successful use — most importantly, an actual write/apply path
exercised live against real hardware, not just a read-only scan.

## Currently experimental

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
