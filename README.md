# Seabass

Reads and manages DJ track libraries across the three catalogs found on a
rekordbox/Engine DJ USB stick: rekordbox's classic per-device export
(`export.pdb`), rekordbox 7's newer unified OneLibrary export
(`exportLibrary.db`), and Denon Engine DJ's library (`m.db`, via the
vendored `libdjinterop`) -- hot cues, memory cues, playlists, and (for
rekordbox/Engine) beatgrid-aware cue writing. See `specs/README.md` for
details on the rekordbox format support, and the project plan for
architecture and current status.

Two ways to use it:

- **`seabass-cli`** -- a command-line tool: `scan` (read-only reporting plus
  duplicate-track cue consolidation), `sync` (match tracks between a
  rekordbox and an Engine source by filename/duration and reconcile their
  cues), and `backups` (list/prune the backups seabass-cli makes before any
  write). Run `seabass-cli --help` for full usage.
- **Seabass** (`seabass`) -- a Qt6 desktop app covering the same
  ground with a UI: browse all three catalogs (including read-only
  OneLibrary browsing), play tracks with waveform/cue display, manually
  merge duplicate tracks, add cues by clicking the waveform, clean up
  orphaned files, and manage backups.

## Status

Scanning, duplicate-track cue consolidation, and cue writing are
implemented for both rekordbox (`RekordboxCueWriter`, via the ANLZ PCO2
sections) and Engine (`LibdjinteropEngineCueWriter`, via `libdjinterop`).
OneLibrary support is read-only (browsing and a best-effort mirror of
writes made through rekordbox/Engine); there is no direct OneLibrary write
path yet. Full bidirectional sync between rekordbox and Engine works via
`seabass-cli sync`, matching by filename and duration.

## License

Seabass's own code is licensed under the **GNU General Public License,
version 2 or (at your option) any later version** (GPL-2.0-or-later). See
[`LICENSE`](LICENSE) for the full GPLv2 text.

### Third-party components

This project vendors a few pieces of other software, each under its own
license (see `.gitmodules` and `specs/README.md` for exact sources):

| Component | Location | License |
|---|---|---|
| `libdjinterop` | `third_party/libdjinterop/` (git submodule) | LGPL-3.0-or-later |
| `kaitai_struct_cpp_stl_runtime` | `third_party/kaitai_struct_cpp_stl_runtime/` (git submodule) | MIT |
| rekordbox PDB/ANLZ format specs, and the C++ parser generated from them | `specs/*.ksy`, `src/infrastructure/rekordbox/generated/` | EPL-1.0 (from [Deep-Symmetry/crate-digger](https://github.com/Deep-Symmetry/crate-digger)) |

**A known, unresolved licensing consideration:** the FSF does not consider
the Eclipse Public License (EPL) 1.0 compatible with the GPL for combined
works -- EPL-covered code can't straightforwardly be redistributed as part
of a GPL-licensed combined binary. The rekordbox format specs (and the
parser code Kaitai Struct generates from them, committed under
`src/infrastructure/rekordbox/generated/`) are EPL-1.0 and unmodified from
their upstream source; they retain that license rather than being
relicensed, but their inclusion in a GPL-2.0-or-later project is a genuine
gray area, not a resolved one. Worth revisiting if this project is ever
distributed more broadly (e.g. by re-implementing that parser
independently, or seeking clarification/relicensing from the specs'
authors) -- flagged here rather than glossed over.

`libdjinterop`'s LGPL-3.0 has no such issue once the GPL side is
"-or-later": GPLv2-or-later permits combining with LGPLv3 code under
GPLv3's terms.
