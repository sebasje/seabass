# Testing against real libraries

Seabass edits other people's music libraries. Synthetic fixtures prove the
code does what we think; only real libraries prove it survives what people
actually have. This describes the corpus of donated libraries, what each of
the three kinds of test needs from it, and how to extend both to every
cleaning and sync function in the app.

The three purposes, in the order they matter:

1. **Data integrity** — does the code produce correct results?
2. **Stability** — does it survive real-world data without falling over?
3. **Speed** — is processing inside a roughly acceptable envelope?

Every one of these must be automated. See the project principle in
`docs/testing.md`: a test somebody has to perform by hand will not get
performed.

## The corpus

Libraries donated through `seabass-cli anonymize` (the "help test Seabass"
flow). The committed one is `tests/fixtures/anonymized_library`, 37 MB,
1118 rekordbox tracks and 1376 Engine tracks. Further sets live outside the
repository and are pointed at by an environment variable, so the repo does
not grow by a gigabyte per contributor.

### What anonymizing keeps, and what that costs a test

Kept as-is, per the export's own manifest: file size, bitrate, duration,
BPM, key, hot and memory cue positions and colours, rating, play count,
last-played date, streaming flag, playlist membership and position, the
monochrome waveform preview, and the beatgrid. Replaced with placeholders:
title, artist, comment, cue comments, file path, playlist names. Removed:
artwork, the detailed waveform data this app never reads, original paths.

Verified on the committed fixture: Engine artists are `Artist <n>` for all
1376 rows in exactly one structural shape, titles are `<id> Track`, paths
are an eight-letter directory plus a numeric filename. The shape is
preserved (739 distinct artists across 1376 tracks) even though the content
is not.

**The cost is that title, artist and path are exactly the fields the
matching logic keys on.** `SyncLibraries` and `DuplicateTrackFinder` pair
tracks by title, artist and duration; Clean Up groups duplicates the same
way. Against anonymized data those matchers run on synthetic strings whose
fuzziness is nothing like real punctuation, feat. spellings, remix
suffixes, accents or case. A matching regression will not necessarily show
up here. Duration, BPM, key and file size are real, so anything keyed on
those is genuinely exercised.

## Does the current data suffice? Partly, and here is the evidence

### Integrity: yes for cue and structural work, no for matching

`tests/anonymized_fixture_integration_test.cpp` already asserts at real
scale: scan counts for both formats, statistics aggregates, a sync matching
floor of 95%, the consistency checker's classification, a real hot-cue
write that round-trips, and the orphaned-file deletion chain. That is a good
base, and it is the right shape.

What it does not touch, from a sweep of the use cases it names:
`DuplicateTrackFinder`, `DuplicateCleanupPlanner`, `ConsolidateDuplicateCues`,
`LibraryCleanupWriter`, `LibdjinteropEngineCueWriter`, `OneLibraryCueWriter`.
So Clean Up, Duplicates, stray-cue removal and both non-rekordbox write
paths have **no real-data coverage at all** today.

### Stability: no, and this is the sharpest gap

The proposed Engine optimisation (one `track::update(snapshot)` per item
instead of three setters) was rejected because `track::snapshot()` throws
for **30 of 50 tracks** on the RV2 stick, a libdjinterop decoder failure on
real performance-data blobs.

Neither the committed fixture nor the newly collected set reproduces it:

| Engine data set | Tracks refused by snapshot + update |
|---|---:|
| RV2 stick (live) | 30 of 50 |
| Collected set, raw Engine copy | 0 of 50 |
| Committed anonymized fixture | 0 of 50 |

All three are Engine schema 3.0.2, so the schema is not the differentiator;
the libraries simply differ. **One data set cannot establish stability.**
The corpus needs to span several libraries, and the harness needs to run
every set rather than one hardcoded path.

### Speed: no, and a corpus alone can never answer it

Timing is a property of the medium, not of the data. The same Engine write,
same code, same library:

| Where the database sits | Per item |
|---|---:|
| On the USB stick | 151.0 ms |
| On a ramdisk | 0.8 ms |

That is a factor of 190. A corpus test running from `/tmp` measures nothing
about how the app behaves on the hardware it exists to serve.

**So do not assert wall-clock seconds in the test suite.** Assert *work
counts* instead: how many times a save opens a database, parses
`export.pdb`, or writes a file. Those are deterministic, portable, fast,
and they catch exactly the regressions that matter here — every performance
bug found so far was "this is done once per item instead of once per save".
Wall-clock envelopes belong in the benchmark tools, run against a real
medium, and are recorded in `docs/write-path-performance.md`.

## Plugging the gaps

### 1. Make the harness corpus-wide, not fixture-wide

Rename `anonymized_fixture_integration_test` to a corpus runner that
discovers sets: the committed fixture always, plus every subdirectory of
`$SEABASS_CORPUS` when set. Each set is a directory holding `rekordbox/`
and/or `engine/` in the anonymizer's own output layout. Every case runs
per set, and the set name goes in every failure message. A set missing a
format skips only the cases needing it.

### 2. Add a stability pass that expects trouble

A pass whose contract is "no crash, no silent wrong answer, and degrade
visibly": for every track in every set, run each reader and each writer
against a scratch copy, catch per-track exceptions, and report them as a
tally rather than aborting. It fails on an unhandled crash, on a write that
reports success while changing nothing, and on a *rise* in the refusal
count against a per-set recorded baseline. That baseline is the mechanism
that turns "30 of 50 tracks have undecodable blobs" from an ambush into a
known, tracked property of a data set.

`tools/engine_write_bench.cpp` already counts refusals this way and is the
prototype for it.

### 3. Count work, do not time it

Add cheap counters to the three writers and the pdb lookup, incremented on
each database open, each `export.pdb` parse, each durable file write. A test
then stages N changes through the real save loop and asserts, for example,
that a 200-item save opens the OneLibrary database once rather than 400
times. This is the automated, cross-platform half of performance testing,
and it would have caught every issue in `docs/write-path-performance.md`
before a stick was ever involved.

### 4. Collect more sets, and fix the anonymizer leak first

Two things about the newly collected set, before any of it is published:

- It contains **both** an anonymized tree and raw stick copies
  (`PIONEER/` and `Engine Library/` alongside `rekordbox/`). The raw Engine
  database has real titles and artists: 0 of 1376 rows carry the
  placeholder pattern, where the anonymized fixture of the same library has
  all 1376. Publishing that set as-is would publish someone's library
  metadata.
- So the collection flow needs a guard: a checker that refuses to accept a
  donated set containing anything outside the anonymizer's own output
  layout, and a verifier that samples the structural shape of title, artist
  and path and fails if they are not uniform placeholders. Both are a few
  dozen lines and both are automatable.

## Applying this to every cleaning and sync function

The matrix below is the target. Each function gets an integrity case (a
correctness assertion), a stability case (survives every set), and a work
count.

| Function | Integrity assertion | Work count |
|---|---|---|
| Sync cue points | every planned cue lands, read back equal; conflicts stay unresolved rather than guessed | one open per format per save |
| Duplicate stats / cue consolidation | consolidation never loses a cue; the merged set is the union | one open per format per save |
| Clean Up duplicates | survivor keeps the union of cues; no surviving row loses a playlist entry; every removed row is in the manifest | one whole-file rewrite per format per save |
| Delete orphaned files | only files no live row references are deleted; manifest cleared exactly for those | one scan per save |
| Library Health repairs | a repaired row resolves to an existing file; an ignored one is untouched | one pdb parse per save |
| Stray cue removal | after saving, a fresh scan finds zero; after undo, exactly the original count | one open per format per save |
| Local cue backup / restore | restored cues equal what was backed up | one open per save |
| Add cue | the cue reads back at the same position and colour; a hot slot replaces rather than duplicates | one open per save |
| Device settings | the written field reads back; other fields byte-identical | one file write per field |

Two of those integrity assertions exist today (sync matching floor, orphan
deletion). The stray-cue one was added to the live suite after a bug slipped
past a test that never re-read what it wrote. The rest are to be written.

**The pattern to copy for every row: write, then read back with a fresh
reader, and assert on what came back.** Every real bug this project has
found in its own write paths was invisible to a test that trusted its own
return value.

## Related

- `docs/write-path-performance.md` — the medium-aware benchmarks and their
  running log of results.
- `docs/testing.md` — the suite as a whole, and the automation principle.
