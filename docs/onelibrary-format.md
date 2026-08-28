# Rekordbox OneLibrary / Device Library Plus format notes

djconvert writes cues into `<PIONEER root>/rekordbox/exportLibrary.db`
("OneLibrary", also called "Device Library Plus") on a stick, alongside
the older `export.pdb` writer (`src/infrastructure/rekordbox/pdb_row_writer.*`).
This is a secondary, best-effort write: see `OneLibraryCueWriter`'s class
comment (`src/infrastructure/onelibrary/onelibrary_cue_writer.hpp`) for why
it never rolls back a primary export.pdb/m.db write that already succeeded.

## What's confirmed

Everything below was verified empirically against a real `exportLibrary.db`
copied read-only from a real stick during development, cross-checked
against [pyrekordbox](https://github.com/dylanljones/pyrekordbox)'s source
(the closest thing to a reference implementation for this format).

- **Location**: `<PIONEER root>/rekordbox/exportLibrary.db`.
- **Encryption**: SQLCipher, using SQLCipher 4's own compiled-in defaults
  (kdf_iter, page size, HMAC/KDF algorithm) -- no `PRAGMA` overrides
  needed. Confirmed by decrypting a real file with exactly `PRAGMA key =
  '<derived key>';` and nothing else, via the `sqlcipher` CLI
  (`mingw-w64-ucrt-x86_64-sqlcipher`).
- **The key is universal**, not per-license or per-machine -- the same
  key opens every OneLibrary export. Rekordbox obfuscates it in its own
  binary (base85 encode, XOR against the ASCII bytes of the string
  `"657f48f84c437cc1"` cycling per byte, zlib-compress) purely to avoid
  it appearing as a plaintext string in a binary scan, not as any kind
  of per-user secret. `onelibrary_key.cpp` reverses exactly this,
  cross-checked against pyrekordbox's `utils.py` (`deobfuscate()`) and
  `devicelib_plus/database.py` (the `BLOB` constant) source.
- **Schema**: `content` (tracks, keyed by `content_id`), `cue`
  (`content_id` FK, `kind`, `inUsec`/`outUsec` microsecond positions,
  plus several legacy frame-addressing columns -- see "What's NOT
  handled" below), `hotCueBankList` / `hotCueBankList_cue` (a newer
  hot-cue-grouping feature, not required for a cue to work as a hot cue
  -- see below), `playlist` / `playlist_content`. Table and column names
  confirmed directly via `.schema` against the real file, not just from
  pyrekordbox's docs.
- **`content_id` is a separate id space from export.pdb's track id.**
  The same file can have different numeric ids in each database (seen
  directly on a real stick: one track was `export.pdb` id 578 but
  OneLibrary `content_id` 566). `OneLibraryCueWriter` therefore matches
  tracks by file path (`content.path`, stick-root-relative,
  forward-slashed, e.g. `/Contents/Artist/Track.mp3`) rather than
  reusing a `sourceId` from the rekordbox side -- the one identifier the
  two databases actually share.

## What's inferred, not confirmed -- re-verify before fully trusting

- **`cue.kind`'s exact meaning.** pyrekordbox's own docstring for this
  column is self-contradictory ("Cue=0, Fade-In=0, Fade-Out=0, Load=3,
  Loop=4" -- three different labels all claiming value 0), and this
  project found no real OneLibrary data anywhere with a populated `cue`
  table to check against -- the stick used during development has never
  had a single cue written to OneLibrary at all (`SELECT count(*) FROM
  cue` returns 0). What djconvert's writer actually does: `kind = 0` for
  a memory cue, `kind = <hot cue number 1-8>` for a hot cue. This is
  based on the **documented** convention of `master.db`'s structurally
  analogous `djmdCue.Kind` column ("0 if memory cue, otherwise the
  number of Hot Cue" -- pyrekordbox's db6 docs), plus prior research
  explicitly describing Device Library Plus's schema as "similar to the
  main Rekordbox database." Well-reasoned, but genuinely unconfirmed
  against real Device Library Plus data. If cues written by djconvert
  show up wrong in Rekordbox (e.g. every cue appearing as a memory cue,
  or hot cue numbers scrambled), this is the first place to look.
- **`colorTableIndex`.** No color-lookup table exists anywhere in this
  schema (checked: none of the 26 real tables is a color palette), and
  no documentation of what indices map to what colors was found.
  djconvert always writes `0` rather than fabricate a mapping from
  `CuePoint::color`'s hex string. Cosmetic only -- doesn't affect cue
  position/hot-cue-number correctness.

## What's NOT handled (deliberately out of scope this pass)

- **Legacy frame-addressing columns** on `cue`
  (`in150FramePerSec`/`inMpegFrameNumber`/`inMpegAbs`/
  `inDecodingStartFramePosition`/`inFileOffsetInBlock`/
  `inNumberOfSampleInBlock`, and their `out*` counterparts) are left
  NULL. Computing them correctly needs per-track encoding parameters
  (sample rate, bitrate, frame size) this pass didn't implement. As of
  when this was written, OneLibrary is only used by newer hardware
  (OPUS-QUAD, OMNIS-DUO, XDJ-AZ, CDJ-3000X) that primarily reads the
  modern `inUsec`/`outUsec` fields, so this is expected to be a
  reasonable simplification rather than a functional gap -- but wasn't
  verified against real hardware.
- **`hotCueBankList`/`hotCueBankList_cue`** (the newer named-bank
  hot-cue-grouping UI feature) is left untouched. A cue's hot-cue-ness
  and slot number live entirely in `cue.kind` per the above; bank
  grouping is an additional organizational layer on top that this pass
  doesn't populate.
- **Write precedent**: no evidence was found anywhere (GitHub issues,
  forums, pyrekordbox's own test suite -- it has no `devicelib_plus`
  tests at all) of anyone else having written to this format and
  reported back, good or bad. Absence of horror stories isn't proof of
  safety. Treat this writer with at least the same caution djconvert
  already applies to its (better-precedented) `export.pdb` writer.
