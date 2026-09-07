# Write-path performance

How to find out where a Seabass save actually spends its time, what the
answer was the first time we asked, and the rules of thumb that came out of
it. Optimizing write paths against slow removable media is recurring work in
this project, so this file is the place to record each round of it rather
than re-deriving the same numbers from scratch.

The short version: **on a USB stick, per-item overhead beats bandwidth, and
CPU work you repeat per item beats both.** Measure before designing. The
first time we did this the obvious culprit (fsync latency) turned out to be
the smaller half, and the fix that mattered had nothing to do with I/O.

## The tools

Two standalone benchmarks, both Qt-free and both safe to point at a real
stick:

- `tools/stick_write_bench.cpp` — splits a save into per-item overhead
  (read-only) and the write strategy itself, comparing per-file flush against
  batched alternatives.
- `tools/onelibrary_tx_bench.cpp` — the cost of one OneLibrary cue write,
  in the shape the code uses today versus one held connection.
- `tools/engine_write_bench.cpp` — the same for Engine, timed on the stick
  and on a ramdisk so the split between device and CPU is visible, and
  against the proposed snapshot-and-update shape.

**Neither writes to the real library.** Everything goes into a scratch folder
under the stick root (`.seabass-writebench`, `.seabass-writebench-tx`) which
they create and delete; `PIONEER`, `Engine Library` and `.seabass-backups`
are only ever read. The OneLibrary tool works on a copy of `exportLibrary.db`
placed in that scratch folder, so it is the same stick and the same
filesystem but never the real file. Keep any new benchmark to that rule.

They are not CMake targets, for the same reason `tools/onelibrary_audit.cpp`
is not: they are occasional instruments, not part of the build. Compile them
against the already-built static library:

```
cmake --build build                                  # so libseabass_core.a exists
g++ -std=c++23 -O2 -I src -I third_party/kaitai_struct_cpp_stl_runtime \
    tools/stick_write_bench.cpp -o build/stick_write_bench \
    build/libseabass_core.a build/librekordbox_format.a \
    build/libkaitai_cpp_stl_runtime.a -lz -ldl -lpthread

build/stick_write_bench /media/you/STICK --device /dev/sdX1 10 50 100 200
build/onelibrary_tx_bench /media/you/STICK 50

# engine_write_bench also needs libdjinterop and its generated config header
g++ -std=c++23 -O2 -I src -I third_party/libdjinterop/include \
    -I build/third_party/libdjinterop/include \
    tools/engine_write_bench.cpp -o build/engine_write_bench \
    build/libseabass_core.a build/third_party/libdjinterop/libdjinterop.a \
    build/librekordbox_format.a build/libkaitai_cpp_stl_runtime.a \
    -lz -ldl -lpthread -lsqlite3
build/engine_write_bench /media/you/STICK 50
```

### Still not measured

- A whole Sync save end to end. The per-item costs above are components; no
  run has yet timed the real save loop over a few thousand staged plans.
  The harness for it would stage N changes against a *copy* of a library on
  the stick and run them through `runSaveLoop`, which is the only way to
  catch costs that live between the items rather than in them.
- The rekordbox ANLZ write in the Sync shape specifically, which unlike the
  stray-cue path has no OneLibrary mirror write beside it.
- Clean Up and Duplicates, whose per-item work is row removals and cue
  merges rather than a cue write, so the numbers here do not carry directly.

## Method, and the trap in it

A USB stick's write latency is not a constant. Its controller does wear
levelling and garbage collection on its own schedule, so a naive benchmark
measures the schedule, not the code.

The first run of `stick_write_bench` did exactly that: it ran the three
strategies in a fixed order and reported that the first one was **thirty-two
times slower per item** than the same strategy measured later in the same
run. The strategy that happens to go first absorbs the warm-up, and whatever
goes last can be charged for work the earlier ones queued in the controller.

So any benchmark against removable media in this project must:

1. **Do a discarded warm-up** before the first timed block.
2. **Rotate the order** of the things being compared across repetitions.
3. **Repeat at least three times and report the median**, and print the
   individual runs so the spread is visible rather than hidden.
4. **Sync the filesystem between blocks**, outside the timer, so one block's
   dirty pages are not charged to the next.

5. **Unmount and remount the stick before every timed run, then let it
   settle** (`--device /dev/sdX1`). This is the one that actually works.
   Each run then starts with a cold page cache and a freshly mounted
   filesystem, and the stick's controller does its background work during
   the idle gap instead of in the middle of a measurement.

With the first four only, expect run-to-run spread of two to seven times on
the write half. With the remount added it collapses to within a second:
three runs of one strategy at 200 cues came out 53.41 / 54.87 / 54.40. That
is the difference between a benchmark that can rank two strategies and one
that cannot. Report the spread either way; if two strategies sit inside it,
the benchmark has not distinguished them.

The remount also shifts every number upward, because a cold mount re-reads
filesystem metadata. That is not a distortion to correct for: a real save
interleaves reads and writes across a 300 MB library, so it runs under
memory pressure much closer to the cold case. The measured components add up
to 105 s of the observed 155 s under the cold condition, against 65 s warm.

Reads are easier: `posix_fadvise(POSIX_FADV_DONTNEED)` drops the page cache
for a file on Linux, which is what `stick_speed_benchmark.cpp` already uses.
There is no equivalent on Windows or macOS.

## Baseline: 2026-09-07, RV2

29 GB exFAT stick on a USB 3 link, 1161 rekordbox tracks, 1469 Engine tracks,
OneLibrary mirror present. Cue files: 1992 `.EXT` totalling 303 MB, mean
156 KB, plus 1992 `.DAT` totalling 16 MB that the cue path never touches.

The case under test: removing stray cues (0:00 memory cues), which are almost
always one per track, so N cues means N distinct analysis files. A live run
of 201 of them took **155 s**.

### Where the time goes, per cue

Rekordbox track, with Engine and OneLibrary also on the stick:

| Work done per cue | Now | If fixed | Why it repeats |
|---|---:|---:|---|
| OneLibrary cue write (two encrypted opens + one transaction) | 235.3 ms | 4.7 ms | A fresh writer per cue; each open re-derives the SQLCipher key from a passphrase |
| Analysis-path lookups (two full `export.pdb` parses) | 15.0 ms | 0.1 ms | Controller resolves the path, then the writer resolves it again |
| Staleness checksums (three whole-file CRC32) | 0.9 ms | 0.0 ms | Constructor, pre-write guard, post-write baseline |
| Cue file backup and rewrite (two durable writes) | 272.0 ms | 272.0 ms | Real work |
| **Measured total** | **523.2 ms** | **276.8 ms** | |

Reference points from the same run: reading all 31.3 MB of the touched cue
files takes 0.66 s cold; building the whole 1161-track id-to-path index in
one pass takes 0.02 s, against 8.4 ms for a single lookup; one SQLCipher open
with `PRAGMA key` costs 118.3 ms, and 0.6 ms once the connection is held.

Those components account for 105 s of the observed 155 s. The rest is
unmeasured work of the same shape (the Engine database reopened per cue, the
stick log opened and closed twice per cue, the backup store rewalking its own
growing directory per file, and a real delete-plus-inserts rather than the
benchmark's single no-op update). Record the gap rather than papering over it.

### The write strategies

Medians of three runs with rotated ordering, writes only, in seconds:

| Strategy | 10 | 50 | 100 | 200 | Syncs at 200 |
|---|---:|---:|---:|---:|---:|
| Per-file flush (today) | 15.37 | 37.25 | 45.59 | 54.40 | 800 |
| Relaxed, one sync at the end | 13.71 | 36.47 | 45.38 | 49.66 | 1 |
| Strict batch via tmpfs mirror | 14.25 | 35.27 | 45.64 | 50.38 | 3 |

**The three strategies are the same speed.** At 100 cues they are within
0.6% of each other; at 200, the widest gap, batching saves 8.7%; at 10 and
50 the ordering between them flips. An earlier warm-cache run appeared to
show batching winning by a factor of two -- that was the stick's mood, not
the code, and it is exactly what the remount exists to prevent. For the
record, that warm run was 1.08 / 3.32 / 6.94 / 14.20 for per-file flush,
1.09 / 2.34 / 4.46 / 6.34 relaxed, 0.55 / 4.89 / 6.43 / 12.16 batched.

Individual runs, to show the spread — this is why the medians above should
not be read to two decimal places:

| Strategy | 10 | 50 | 100 | 200 |
|---|---|---|---|---|
| Per-file flush | 12.81 / 15.52 / 15.37 | 40.25 / 36.85 / 37.25 | 46.19 / 45.50 / 45.59 | 53.41 / 54.87 / 54.40 |
| Relaxed | 13.91 / 13.71 / 13.69 | 37.59 / 32.53 / 36.47 | 47.01 / 44.78 / 45.38 | 50.51 / 49.66 / 48.77 |
| Strict batch | 14.25 / 14.07 / 14.27 | 34.19 / 35.27 / 36.00 | 46.03 / 44.90 / 45.64 | 50.38 / 49.77 / 50.67 |

Bytes moved: 10 cues is 1.4 MB read and 2.7 MB written (each file is backed
up and rewritten); 50 is 7.2 / 14.4; 100 is 16.3 / 32.5; 200 is 31.3 / 62.7.

### Projected whole save

Measured per-cue overhead plus the measured median write time:

| Cues | Today | Repeated work removed | Plus batched writes |
|---:|---:|---:|---:|
| 10 | 17.9 s | 15.5 s | 13.8 s |
| 50 | 49.8 s | 37.5 s | 35.5 s |
| 100 | 70.7 s | 46.1 s | 45.9 s |
| 200 | 104.6 s | 55.4 s | 50.7 s |

## Round 2, same day: the cost is different per format

The stray-cue round measured the rekordbox and OneLibrary paths. Sync,
Clean Up, Duplicates, Local Cue restore and Add Cue all go through the same
three cue writers, so the obvious question is whether the same answer
applies. It does not: **the right fix differs per format**, and the
scratch-copy mechanism the project already has helps exactly one of them.

`tools/engine_write_bench.cpp`, 50 tracks from RV2's own Engine Library,
copied to a scratch directory on the stick and to a ramdisk:

| One Engine cue write | Per item |
|---|---:|
| Today, database reopened per item, on the stick | 151.0 ms |
| Today, database reopened per item, on a ramdisk | 0.8 ms |
| One handle held, one `track::update(snapshot)` per item | 0.4 ms |

Put beside the OneLibrary figures from round 1, the three formats want
three different things:

| Format | Per item | Where the time goes | Fixed by |
|---|---:|---|---|
| Engine | 151 ms | almost entirely device I/O | a scratch copy: 151 ms to 0.8 ms |
| OneLibrary | 235 ms | almost entirely CPU (two PBKDF2 key derivations per call) | holding the connection: 235 ms to 4.7 ms. A scratch copy does **not** help |
| rekordbox | 15 ms + a whole-file ANLZ rewrite | two `export.pdb` parses, then per-track file writes | an index built once per save; the per-track writes are irreducible |

Two consequences worth carrying forward:

- **Sync's Engine target is already fast** and nobody noticed. It goes
  through `FormatWriteSession`, which scratch-copies `m.db` once the item
  count clears its threshold, so those writes land on a ramdisk at 0.8 ms
  each. The stray-cue path does not use `FormatWriteSession` at all, so its
  Engine writes pay the full 151 ms.
- **A scratch copy is not a general speed fix.** It converts device I/O into
  RAM I/O, so it does nothing at all for a cost that is CPU. That is exactly
  the OneLibrary case, and it is why "put it on a ramdisk" was the wrong
  instinct there.

### A proposed optimization the benchmark rejected

Collapsing the Engine writer's three auto-commit updates into one
`track::update(snapshot)` looked free: the library creator already uses that
shape, and it measured faster. It cannot be used. On RV2's real Engine
Library, `track::snapshot()` **throws for 30 of 50 tracks** ("Track data blob
doesn't have expected decompressed length"), the same libdjinterop decoder
quirk the cue writer already works around for `sample_rate()`. The current
three-setter shape writes all 50 without complaint.

Round-tripping a whole snapshot means round-tripping every field, including
ones this library cannot decode. Keep the targeted setters. This is the
clearest argument in this file for measuring against a real library rather
than a fixture: a synthetic Engine database created by `create_database()`
has none of these tracks and would have passed.

## What came out of it

**Removing repeated work halves the save. Changing the write strategy is
worth under 9% at the largest size and nothing at all below it, and costs
either a safety guarantee or a new abstraction.** Batching flushes was
the idea we started with; it is the smaller prize and the only one that
trades away either a safety guarantee or a new abstraction.

Rules of thumb for the next round:

- **Suspect per-item CPU before per-item I/O.** The single largest cost here
  was a key derivation, not a disk write. Anything that opens an encrypted
  database, parses a whole file, or checksums a whole file *per item* is a
  candidate worth more than any write-strategy change.
- **Count the opens.** Reopening a database per item is the recurring shape
  of this bug in this codebase. It has now been found in the Engine cue
  writer, the OneLibrary cue writer, and the analysis-path lookup, and it was
  the documented reason the Engine library creator builds in a scratch
  directory (`libdjinterop_engine_library_creator.cpp`).
- **Resolve once per save, not once per item.** A save knows its whole work
  set up front. Indexes, connections and writer objects belong in
  `SaveContext::shared`.
- **Do not batch durability without a measurement that justifies it.** The
  per-file flush is the strongest guarantee available and, at these sizes, it
  is not what costs the time.
- **Scratch copies are for one big file, not many small ones.**
  `FormatWriteSession` already handles the one-database case well. The
  per-track analysis files are a different shape and did not benefit.

## Platform notes

The findings above are Linux. Anything batched diverges elsewhere:

- **Windows** has no user-mode filesystem sync. Flushing a volume needs
  administrator rights, so a batch degrades to one flush per file in two
  ordered passes: the ordering guarantee survives, the flush saving does not.
  A batched rename pass must also retry on sharing violations, the way
  `compact_stick_backup.cpp` already does with `MoveFileEx` and backoff.
  Note that `durable_file_write.cpp` does not apply `longPathSafe`, so it
  cannot reach a path past `MAX_PATH`.
- **macOS** is not a build target: the non-Windows branch of the build is
  Linux-specific. If it ever becomes one, batching pays off *more* there,
  because `F_FULLFSYNC` (which `writeFileDurably` already uses on Apple,
  since plain `fsync` does not reach the drive) is markedly more expensive
  than a Linux `fsync`.

Benchmarks themselves are currently Linux-only: they depend on `syncfs`,
`posix_fadvise` and `udisksctl`. That is a gap to close, not a fact to live
with. The right shape is a C++ driver built against this project's own
platform adapters (`createRemovableMediaMounter()`,
`createRemovableMediaLocator()` in `src/infrastructure/media/`), so the same
program unmounts and remounts a stick on both platforms, rather than a shell
script full of `lsblk`, `findmnt` and `udisksctl` plus a PowerShell twin of
it. Note that Windows numbers will not be comparable to these regardless:
there is no user-mode filesystem sync there, so a batch degrades to per-file
flushes.

**No test in this project is a human checklist.** It is a one-man project;
anything that needs a person to follow steps will not get run. A test is
either an automated script or a C++ program, triggered manually if need be
but never performed manually.

## Adding a round

Keep this file as a running log. For each round, record: the stick and its
filesystem, the case under test, the per-item breakdown, the strategy
comparison with individual runs, and what the decision was. A number without
the stick it came from is not reusable.

## Round 3, 2026-09-07: one index per save instead of two parses per item

The first fix from Step 3 of the plan, and the first one the corpus
runner's work counts could prove rather than argue for.

`findAnlzPathForTrackId()` parses the whole 1.4 MB `export.pdb` to answer
one question, and a rekordbox save asked it twice per item: once in the
change class to find the analysis file to back up, once inside
`RekordboxCueWriter` to find the same file to write. A 200-item save
therefore parsed the same database 400 times.

`infrastructure/rekordbox/anlz_path_index` builds the whole map in one
pass. The save holds one per catalog in `SaveContext::shared`, every change
class asks it instead of the database, and `RekordboxCueWriter` takes an
optional pointer to it. The single-shot callers, the command line and the
waveform reader, keep the old lookup: for one id, one pass is the cheaper
answer.

Measured by `corpus_test`, which records these per data set and fails on a
change:

| Save | Parses before | After |
|---|---:|---:|
| Add one cue | 2 | 1 |
| Remove 5 stray cues | 10 | 1 |
| Library Health repair | 2 | 1 |
| Clean Up one group | 2 | 2 |

The stray-cue row is the shape of the win: flat in the number of items
rather than linear. Extrapolated to the 201-cue save that started all of
this, 402 parses become 1.

Clean Up is unchanged because its second parse is `PdbRowWriter`'s own read
of the database it is about to rewrite, which is a different cost and needs
a different fix.

A failure is not automatic if the index cannot be built: the catalog may be
unreadable, and every caller falls back to the per-call lookup rather than
failing the save.

**Still open, in order:** hold one OneLibrary connection and key derivation
per save (the corpus counts show 1 to 5 SQLCipher opens per save today,
each paying a PBKDF2 derivation, which is the 235 ms/item cost), reuse the
Engine database handle, keep the operation log's stream open, and fix the
two O(n^2) loops in `FilesystemBackupStore`.

## Round 4, 2026-09-07: one OneLibrary connection per save

The 235 ms per item. Every SQLCipher open derives the key from a
passphrase, roughly 115 ms of CPU, and the writer opened two connections
per call: one to write through, one to read the committed result back.
Nine call sites then constructed a writer per item, so a save paid that
derivation once per item at least, often twice.

Two changes, both needed. `OneLibraryCueWriter` now holds its two
connections instead of opening them per call, and the save holds one writer
per database in `SaveContext::shared` instead of nine call sites each
building their own.

The verification keeps its own separate connection rather than re-reading
through the one that just wrote. Reading the committed result back through
a different connection is the property that check exists for; what it did
not need was a fresh key derivation each time to do it. Holding the
connections is safe because every method checks the staleness guard first
and throws if the file changed underneath, so a held connection is never
used against a database the writer no longer recognises.

Measured on the real RV2 set by `corpus_test`:

| Save | SQLCipher opens before | After |
|---|---:|---:|
| Remove 5 stray cues | 5 | 1 |
| Library Health repair | 4 | 2 |
| Clean Up one group | 4 | 2 |

Flat in the number of items rather than linear, which is the shape that
matters. The remaining 2 is the floor: one write connection and one verify
connection, opened once per save.

The first attempt at this changed nothing measurable, because holding the
connections inside a writer that is still constructed per item just moves
where the derivation happens. The counts said so immediately, which is the
argument for having them.

**Still open, in order:** reuse the Engine database handle (20 opens for 20
items today), keep the operation log's stream open, and fix the two O(n^2)
loops in `FilesystemBackupStore`.

## Round 5, 2026-09-07: one Engine handle per save

`load_database()` is a full SQLite open plus schema detection, about 151 ms
against a stick, and every method did one: a 200-item save opened the same
database 200 times. The writer holds it now, opened lazily so constructing
a writer that is never used stays free. Add Cue also shared its Engine
writer for the save, the way the OneLibrary one already is.

Measured by `corpus_test`, writing 20 cues:

| | Engine opens |
|---|---:|
| Before | 20 |
| After | 1 |

Safe to hold: libdjinterop writes through its own transactions, and nothing
else in this process writes that file while a save owns the writer.

The recorded metric changed shape for the third time, and for the same
reason each time: a per-item average rounds a 20-to-1 win down to zero. It
now records the total for the batch, which is the number that says whether
the cost is flat or linear.

**Still open:** keep the operation log's stream open, and the two O(n^2)
loops in `FilesystemBackupStore`.

## Round 6, 2026-09-07: re-measured on the stick, and it changes the plan

`tools/staged_save_bench` times the workload that started all of this:
remove a 0:00 memory cue from N tracks through the real save loop, staged
exactly as the Library Health page stages them, against files on a real
stick. It works only inside `<stick>/.seabass-savebench/` and removes it
afterwards.

Measured on RV2, the same stick as the original 155 s / 201-cue save:

| Items | Wall clock | Per item | pdb parses | SQLCipher opens | Durable writes |
|---:|---:|---:|---:|---:|---:|
| 50 | 9.9 s | 198 ms | 1 | 2 | 101 |
| 201 | 135.8 s | 676 ms | 1 | 2 | 403 |

Two things to take from this, and the second matters more than the first.

**The repeated work is gone and stays gone.** One catalog parse and two key
derivations for the whole save, at any batch size. That was 402 parses and
201 derivations for the 201-item save. The counts are flat, which is what
rounds 3 to 5 set out to do.

**And it barely moved the clock: 155 s became 136 s.** Removing per-item CPU
work bought about 12%, because the save is now bound almost entirely by
per-file durable writes. 403 of them for 201 items -- one backup and one
target per cue -- and at the ~330 ms a flushed whole-file write costs on
this stick, that is essentially the entire 136 s.

**Worse, it is superlinear.** Four times the items cost thirteen times the
wall clock, and the per-item figure triples from 198 ms to 676 ms.
*(Round 8 retracts this: the same 200-item batch measured 659 ms and 278 ms
per item in one sweep. These runs did not follow round 2's own rotate,
remount and median discipline, so run order dominated them. The rest of
this section stands; the superlinearity does not.)* Nothing
in the work counts grows, so this is not repeated work coming back. The
most likely cause is the backup directory: every file a save backs up lands
in one flat directory, exFAT scans a directory linearly to insert into it,
and this save puts 201 files into one. That would make the backup half
quadratic in the number of items, in the filesystem rather than in our
code, caused by our choice of layout.

**This reverses round 2's conclusion, with evidence.** Round 2 found the
write strategy irrelevant, within 9% across three durability options. That
was measured while per-item repeated work dominated everything. It no
longer does, so the write half is now the whole cost, which is exactly the
condition Step 4 of the plan named for revisiting batching rather than
guessing at it.

**Next, in order:**

1. Confirm the superlinearity and its cause before fixing anything. Time
   the save's two halves separately, and time the same batch with the
   backups written into per-item subdirectories instead of one flat one.
   The hypothesis is specific and cheap to falsify.
2. If it is the directory, shard the backup layout. That is a small change
   and needs no batching.
3. Only then revisit batched durability, with these numbers rather than
   the old ones.

**Caveat on this run.** The stick was unmounted while a third measurement
was in progress, so a `.seabass-savebench` directory may be left on it.
Nothing else writes there and it is safe to delete. The 50 and 201 figures
above are from runs that completed and cleaned up after themselves.

## Round 7: the flat-directory hypothesis was wrong

Round 6 guessed that the superlinearity came from every backup landing in
one flat directory that exFAT has to scan linearly to insert into. That is
wrong, and the cheapest way to find out was to take the USB link out of the
picture and keep the filesystem: the same benchmark against an exFAT
loopback image on the system SSD.

| Items | Per item, exFAT on SSD |
|---:|---:|
| 25 | 19.4 ms |
| 50 | 13.3 ms |
| 100 | 11.7 ms |
| 200 | 11.1 ms |

Flat, in fact improving per item as the fixed setup amortises. Same
filesystem, same flat backup directory, same 403 durable writes for 200
items, no bend at all. So neither exFAT's directory handling nor our
backup layout explains the curve on the stick.

That leaves the medium, and two candidates worth separating before
anything is designed around either:

- **Write-cache saturation.** Small flash devices absorb the first burst
  into a fast buffer and fall back to slower direct writes once it fills.
  A longer run would then cost more per item than a short one, which is the
  shape observed.
- **Allocation on a nearly full volume.** RV2 was at 93% before these runs
  and 96% after, and the benchmark itself copies 600 MB onto it each time.
  exFAT allocation slows as free space fragments, so a run that fills the
  medium as it goes bends for reasons unrelated to the code.

The second is the more likely of the two and the easier to rule out: run
the sweep on a stick with plenty of free space, in ascending and then
descending order of batch size. If the curve follows the batch size it is
the device; if it follows the fill level it is the volume. The benchmark
now prints free space with every run so the two cannot be confused.

**Either way the conclusion for our code is the same**, and it is the one
that matters: every work count is flat, one catalog parse and two key
derivations per save at any batch size, and the remaining cost is per-file
durable writes to a slow medium. What changes with the answer is only
whether batching is worth its complexity, and that question is still open
rather than settled either way.

## What moving backups off the stick would buy

Sebastian's suggestion, and the arithmetic supports it. Of the 403 durable
writes a 201-cue save makes, 201 are backup copies and 202 are the actual
library writes. The loopback numbers put a durable write on the system SSD
at roughly 11 ms against roughly 330 ms on the stick, so those 201 backup
writes are close to the whole of half the save's cost, and moving them to
local disk would remove essentially all of it. Expect a save to roughly
halve.

What it costs is not performance but a property the current design has:
backups live at `<stick>/.seabass-backups`, so undo travels with the stick.
Move them to the local machine and Undo Last Save only works on the machine
that performed it, a stick carried to another computer has no way back, and
the backup no longer survives the loss of that computer. For a feature
whose entire job is "you can get your library back", that is a real
trade-off and not merely a location change.

A middle option keeps both: write the backups to local disk during the
save, where they are nearly free, then copy them to the stick once at the
end, as **one archive rather than 201 files**. The stick then pays one
sequential write instead of 201 flushed ones, undo still travels with the
stick, and the project already has an archive writer and reader for exactly
this shape (`infrastructure/zip_archive_writer` and `_reader`, and the
stick-backup feature's own append-only archive with its journal).

This is a design decision rather than an optimisation, so it wants deciding
rather than assuming. Recorded here so the next round starts from it.

## Round 8: the superlinearity was not real, and I ignored our own method

The sweep, ascending then descending in one session, on RV2 after a
filesystem check and with the old backups cleared:

| Items | Ascending | Descending | Free space |
|---:|---:|---:|---:|
| 25 | 906.6 ms | 429.9 ms | 0.53 GiB |
| 50 | 253.9 ms | 270.9 ms | 0.53 GiB |
| 100 | 299.7 ms | 269.3 ms | 0.52 GiB |
| 200 | **659.1 ms** | **277.9 ms** | 0.50 GiB |

The two 200-item runs are the whole answer. Same batch size, same code,
same free space to three decimal places, one immediately after the other,
and they differ by 2.4x. Whatever that is, it is not a property of the
batch size, and round 6's superlinearity does not survive it. Neither does
the fill-level explanation: free space is identical across the pair.

Corrected numbers, then. For batches of 50 and up the per-item cost is
roughly flat at 250 to 300 ms, with one outlier at 659. Against the
original 771 ms per cue that is a real improvement of about 2.5 to 3x, not
the 12% round 6 reported from a single unlucky run. A 200-cue save on this
stick lands somewhere between 56 s and 132 s depending on nothing the code
controls.

The 25-item runs being the most expensive per item is real and expected:
the fixed cost of a save (one catalog parse, two key derivations, the write
session's setup) divided by 25 rather than 200. It is the same absolute
cost, showing up larger.

**The actual lesson is procedural and it is on me.** This document's own
round 2 established the method for measuring a stick, in its own words:
rotate the order and take the median of three, and unmount and remount
between runs, because run order otherwise dominates the result. I built a
new benchmark and ran it single-shot, in order, without remounting. Round
6's conclusion, its hypothesis, and the round 7 experiment chasing that
hypothesis all follow from that one omission. The loopback result in round
7 is still valid and still useful -- exFAT is not the problem -- but it was
answering a question that should not have been asked yet.

**Before any further conclusion about the write half:** give
`staged_save_bench` the same `--device` unmount and remount that
`stick_write_bench` already has, and the same rotate-and-median discipline.
Until then the honest summary is that the work counts are flat and provably
so, the per-item wall clock is roughly flat for realistic batch sizes, and
the save is two to three times faster than it was.

**Unchanged by any of this:** 201 of the 403 durable writes in a 201-cue
save are backups, and moving them off the stick, or batching them into one
archive written at the end, remains the largest single lever available. It
does not depend on which of these numbers is right.
