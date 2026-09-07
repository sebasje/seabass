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

build/stick_write_bench /media/you/STICK 10 50 100 200
build/onelibrary_tx_bench /media/you/STICK 50
```

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

Even with all four, expect run-to-run spread of two to seven times on the
write half. Report it; do not average it away. If two strategies are within
that spread of each other, the benchmark has not distinguished them.

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
| Analysis-path lookups (two full `export.pdb` parses) | 16.8 ms | 0.1 ms | Controller resolves the path, then the writer resolves it again |
| Staleness checksums (three whole-file CRC32) | 0.6 ms | 0.0 ms | Constructor, pre-write guard, post-write baseline |
| Cue file backup and rewrite (two durable writes) | 71.0 ms | 71.0 ms | Real work |
| **Measured total** | **323.7 ms** | **75.8 ms** | |

Reference points from the same run: reading all 31.3 MB of the touched cue
files takes 0.66 s cold; building the whole 1161-track id-to-path index in
one pass takes 0.02 s, against 8.4 ms for a single lookup; one SQLCipher open
with `PRAGMA key` costs 118.3 ms, and 0.6 ms once the connection is held.

Those components account for 65 s of the observed 155 s. The rest is
unmeasured work of the same shape (the Engine database reopened per cue, the
stick log opened and closed twice per cue, the backup store rewalking its own
growing directory per file, and a real delete-plus-inserts rather than the
benchmark's single no-op update). Record the gap rather than papering over it.

### The write strategies

Medians of three runs with rotated ordering, writes only, in seconds:

| Strategy | 10 | 50 | 100 | 200 | Syncs at 200 |
|---|---:|---:|---:|---:|---:|
| Per-file flush (today) | 1.08 | 3.32 | 6.94 | 14.20 | 800 |
| Relaxed, one sync at the end | 1.09 | 2.34 | 4.46 | 6.34 | 1 |
| Strict batch via tmpfs mirror | 0.55 | 4.89 | 6.43 | 12.16 | 3 |

Individual runs, to show the spread — this is why the medians above should
not be read to two decimal places:

| Strategy | 10 | 50 | 100 | 200 |
|---|---|---|---|---|
| Per-file flush | 3.27 / 1.08 / 0.62 | 3.02 / 4.97 / 3.32 | 8.59 / 6.32 / 6.94 | 22.93 / 14.20 / 13.44 |
| Relaxed | 2.92 / 1.09 / 0.23 | 2.34 / 1.28 / 3.12 | 8.16 / 2.71 / 4.46 | 6.34 / 5.68 / 8.93 |
| Strict batch | 1.12 / 0.55 / 0.28 | 4.91 / 1.33 / 4.89 | 21.14 / 4.65 / 6.43 | 12.16 / 7.16 / 53.04 |

Bytes moved: 10 cues is 1.4 MB read and 2.7 MB written (each file is backed
up and rewritten); 50 is 7.2 / 14.4; 100 is 16.3 / 32.5; 200 is 31.3 / 62.7.

### Projected whole save

Measured per-cue overhead plus the measured median write time:

| Cues | Today | Repeated work removed | Plus batched writes |
|---:|---:|---:|---:|
| 10 | 3.6 s | 1.1 s | 0.6 s |
| 50 | 16.0 s | 3.6 s | 2.6 s |
| 100 | 32.2 s | 7.4 s | 5.0 s |
| 200 | 64.7 s | 15.2 s | 7.3 s |

## What came out of it

**Removing repeated work was worth about four times what changing the write
strategy was worth, and cost nothing in crash safety.** Batching flushes was
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
