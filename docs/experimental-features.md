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

- **Library Health** (added 2026-08-29) — cross-catalog consistency
  scan/repair, plus the 0:00-junk-memory-cue cleanup. Repair-writes
  (`repairAll`/`repairOne`/`deleteOrphan`/`removeJunkCue`/`removeAllJunkCues`)
  are verified only via extensive live read-only scans and disposable
  diagnostics against real hardware so far, never an actual apply. Promote
  to stable once a real repair/removal has been run successfully a few
  times without incident.

- **Stick Statistics** (added 2026-08-29) — filesystem/hardware info,
  per-catalog library stats, a Filelight-style disk usage breakdown, and a
  local read-speed benchmark with history. Read-only (never writes to the
  stick), but built entirely in one autonomous session without Sebas able
  to click through the actual running UI to confirm it renders/behaves
  correctly live — verified via unit tests, real-hardware diagnostics for
  every domain/infrastructure piece, and a clean build through Qt's own
  QML compiler, but the QML page itself is unclicked. Needs a real
  look-through before promoting, independent of the write-path bar every
  other entry here is judged against.

## Graduated to stable

(none yet)
