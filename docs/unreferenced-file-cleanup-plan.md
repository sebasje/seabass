# Cleaning up unreferenced audio files

## The problem

A stick accumulates audio files that no catalog references any more:
rekordbox's `export.pdb`, Engine's `Database2/m.db` and OneLibrary all
have no row pointing at them. They are invisible to every feature we
have, because every feature starts from a catalog and works outwards.

They arise the way you would expect: a duplicate cleanup (ours, or the
DJ software's, or a hand edit) removes the *rows* for a redundant copy
and never removes the *file*. "Delete Orphaned Files" covers the case
where we did it ourselves and recorded it in
`.seabass-pending-deletions.jsonl`. It cannot cover a cleanup that
happened before that manifest existed, on another machine, or in
rekordbox itself -- the manifest simply has no entry, so the file is
lost to us forever.

## What is actually out there (RV2, 2026-09-08)

Measured on the real 30 GB stick, 96% full with 1.5 GB free:

| | |
|---|---|
| audio files on disk | 2100 |
| referenced by no catalog | **632** (8.66 GB) |
| of those, readable by a tag parser | 632 |
| with both artist **and** title in their tags | 632 |
| with a duration | 632 |
| grouping with a catalogued track on artist+title+duration±2s | **632** |
| grouping with nothing at all | 0 |

Comparing each against its best catalogued twin, byte for byte:

- **598 are byte-identical** to a file the catalog still references
  (8.14 GB) -- free space with no judgement call in it at all
- **28** are a lower-or-equal quality copy: the ordinary duplicate
  decision `DuplicateCleanupPlanner` already makes
- **6** are the same size but *not* byte-identical -- almost certainly
  the same audio with differently-padded tags, but not proven so, and
  "not proven" is the only category that matters when the next step is a
  delete. They go through the ordinary duplicate review like any other
  group. (An `ffmpeg -f md5` comparison of the decoded audio was tried
  and thrown away: run against a busy USB bus it reported five
  *byte-identical* pairs as having different audio, which is impossible.
  A method that produces false "these differ" answers is useless for
  clearing files, so the byte-level hash is the one we trust.)
- **0** are better than anything the catalog has -- nothing here is a
  file we would want to keep *over* a catalogued one

The names say where they came from: `-1`/`-2` copy suffixes and
truncated rekordbox export names, i.e. exactly the "rows removed, files
left" shape described above.

So the metadata in the files agrees with what the catalogs recorded, for
every single one. This needs no fuzzy matching and no new grouping
rules: the existing `DuplicateTrackFinder` key (artist + title +
duration within tolerance) already works on it, once something puts the
tags in front of it.

## Reading the metadata: TagLib

We already have `TrackDurationProbe` / `QtMultimediaDurationProbe`, so
the obvious question is whether to widen that. We should not.

`QMediaPlayer` metadata is a side effect of opening a demuxer:
asynchronous, needs a `QCoreApplication` and a nested event loop, and
*which* fields get populated depends on the backend -- FFmpeg on Linux,
MediaFoundation on Windows and AVFoundation on macOS do not agree. That
is a per-platform behaviour difference feeding a destructive code path.
It also gives no dependable bitrate, and bitrate is precisely what
`DuplicateCleanupPlanner` picks the survivor by.

TagLib is the small, self-contained, cross-platform thing this calls
for. Pure C++, no Qt, no event loop, synchronous. One open yields
artist, title, album *and* `audioProperties()` -> length, bitrate,
sample rate: everything both the finder and the planner need. It reads
headers and tag frames rather than the stream, so it moves far fewer
bytes over USB than a demuxer open. It is 326 kB of shared library.

Measured on this machine, same warm 400 files, apples to apples:

| | total | per file | gives |
|---|---|---|---|
| TagLib 1.13.1 | 29 ms | **0.07 ms** | artist, title, duration, bitrate |
| `QtMultimediaDurationProbe` spike | 2997 ms | 7.5 ms | duration |

**103x faster**, and it answers three more questions per file. Over the
whole stick, all 2100 files: 22-66 s cold over USB (I/O bound, varies
with where the reads land), 151 ms fully warm. Coverage was total --
2100/2100 gave a duration and a bitrate, 2099/2100 gave artist+title.

Because it is Qt-free it can sit below `seabass_core` and work in
`seabass-cli` and the Qt-free `corpus_test`, which the Qt probe never
can.

Packaging is a one-liner everywhere we build: `libtag1-dev` on
Debian/Ubuntu (note: *not* `taglib-dev`, and `apt-cache search taglib`
does not match it), `mingw-w64-ucrt-x86_64-taglib` in MSYS2 alongside
every other dependency in `docs/windows-build.md`, homebrew or vcpkg on
macOS. Vendoring as a submodule is the fallback, the same shape as
libdjinterop -- use 1.x, since 2.x pulls in utf8cpp. Licence is
LGPL-2.1/MPL-1.1, fine to link from our GPL-2.

Alternatives considered and rejected: `dr_libs`/`miniaudio` are
single-header and cross-platform but decode-only, with no tag reading at
all; `libavformat` directly is heavy and a packaging problem on Windows.

### The one caveat, and why it decides a safety rule

For a VBR MP3 with no Xing/VBRI header, TagLib does not know the real
length -- it estimates it from the bitrate, and can be off by seconds.
The grouping tolerance is 2 seconds, so an estimate can put a file in
the wrong group, or in no group.

How often does that bite? On RV2, never: of the 1857 MPEG files on the
stick, **every single one carries a Xing or VBRI header**, so TagLib
reads a real length rather than estimating one. The other 243 files are
MP4/M4A, whose length comes from the container header and is exact by
construction. Zero files on this stick land in the uncertain bucket, and
zero of the 632 unreferenced ones do.

The rule below therefore costs us nothing here. It still has to exist,
because another stick, another rip or an older encoder will produce a
headerless VBR MP3 eventually, and the cost of noticing that only after
deleting the file is unrecoverable.

That never matters for a track we merely *display*. It matters enormously
here, because the outcome of this feature is deleting a file that no
database will ever mention again. So:

> **A file whose duration we had to estimate is never deleted.**

We list it, we say why, and we leave it on the stick. Being wrong costs
the DJ a track they own; being conservative costs them disk space they
can reclaim by hand. Those are not comparable. The probe therefore has
to report duration *confidence*, not just a number -- a bare `double`
cannot express "roughly", and a caller that cannot tell the difference
will eventually treat a guess as a fact.

Detecting it is cheap and exact: `TagLib::MPEG::Properties::xingHeader()`
returns null precisely when TagLib had to fall back to
`streamLength / bitrate`. (`XingHeader` covers VBRI too.) Every non-MPEG
format we meet reports a container-derived length, which is never
estimated.

## Design

### 1. `TrackMetadataProbe`

A new port beside `TrackDurationProbe`, returning the whole set at once:

```
struct FileMetadata {
    std::string title, artist, album;
    double durationSeconds = 0.0;
    bool durationIsEstimated = false;   // see the safety rule above
    int bitrate = 0;
    int sampleRate = 0;
};
```

`TagLibMetadataProbe` implements it in a new `seabass_taglib` target,
optional at configure time exactly as `Qt6::Multimedia` already is, with
a null implementation when TagLib was not found. `fillMissingDurations`
keeps working off `durationSeconds`, so existing callers are unaffected
-- and get a much faster probe for free.

### 2. Metadata cache

Mirror `DurationCache` exactly: JSON Lines at
`<stickRoot>/.seabass-metadata.jsonl`, one flat object per line, paths
relative to the stick root so a cache written on Linux still reads on
Windows, and an entry trusted only when size **and** mtime both still
match. Same strictness for the same reason: a stale answer here feeds a
destructive caller.

Scanning 2100 files costs 22-66 s cold; with the cache the second scan
of an unchanged stick costs a `stat` per file. Worth having from the
start rather than bolted on later.

### 3. Finding the unreferenced files

Walk `Contents/` via `infrastructure/long_paths.hpp` -- **not**
`fs::recursive_directory_iterator`, whose MAX_PATH limits that header
documents are real and would silently skip deep paths on Windows.
Subtract every path referenced by rekordbox **and** Engine **and**
OneLibrary. Checking a single format would delete files the other
catalog still plays; that is the one bug in this feature that would be
unforgivable, so the "all catalogs" requirement belongs in the API
shape, not in a comment.

### 4. Grouping

Probe each unreferenced file, build a `domain::Track` with
`format = "disk"`, and feed it into the *existing*
`DuplicateTrackFinder::find()` alongside the catalogued tracks. No new
matching logic.

`DuplicateCleanupPlanner` needs one new rule, and it is subtler than
"never pick an unreferenced file":

> An unreferenced file may be the survivor **only when every copy in the
> group is unreferenced.** If any copy in the group is catalogued, the
> survivor must be a catalogued one.

The reason for both halves: keeping an unreferenced file over a
catalogued one would leave the catalog pointing at a deleted file (we
would have to rewrite the row to point at the survivor -- a different,
larger feature). But a group of *only* unreferenced files is a real
case: several stray copies of a track that fell out of every catalog. We
should still collapse those down to one, and the best copy wins on the
usual bitrate/length rules. The result is a single stray file, which is
then a candidate for re-import rather than for deletion.

### 5. What the DJ sees

Unreferenced files are **offered on the Clean Up Duplicates page
first**, as part of the normal duplicate review, so they can be
deduplicated with everything else and with the same survivor/merge
information in front of the DJ. Only what survives that review -- files
confirmed redundant and not kept -- is routed onward to "Delete
Orphaned Files" for the actual deletion. Not the other way round: the
deletion page is the last step, never the place a file is first
encountered.

`resolvePendingDeletions`' fresh-scan re-check stays the final safety
gate, unchanged in spirit but widened to check every catalog rather than
one format's.

Files matching nothing are never touched, only listed. Zero of those on
RV2, but on another stick they are real music that fell out of the
database -- re-import candidates, not deletion candidates.

Optional, off by default: a "verify byte-identical" step that content-
hashes an unreferenced file against its catalogued twin, turning
"metadata says duplicate" into certainty. That is what established
598/632 above. It is an 8 GB read over USB, so it belongs behind a
toggle rather than in the default path.

## Order of work

1. `TrackMetadataProbe` port + `TagLibMetadataProbe` + optional CMake
   wiring + unit tests over `testdata/`
2. `MetadataCache` mirroring `DurationCache`, with its tests
3. Unreferenced-file walk (all three catalogs), with tests
4. `DuplicateCleanupPlanner` survivor rule + the estimated-duration
   exclusion, with tests for both
5. Clean Up Duplicates page shows unreferenced files in the review
6. Route confirmed-redundant ones to Delete Orphaned Files
7. Live run on RV2 against the numbers in this document
