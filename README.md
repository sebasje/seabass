# djconvert

A command-line tool to read and (eventually) sync DJ track libraries
between rekordbox USB exports and Denon Engine DJ libraries -- hot cues,
loops, beatgrid, and playlists. See `specs/README.md` for details on the
rekordbox format support, and the project plan for architecture and
current status.

## Status

Read-only scanning (`djconvert scan`) and duplicate-track cue
consolidation are implemented for Engine libraries; rekordbox duplicate
detection/reporting works too, but rekordbox itself has no write path yet.
Full bidirectional sync between the two formats is still in development.

## License

djconvert's own code is licensed under the **GNU General Public License,
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
