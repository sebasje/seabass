# Library Synchronization: Cue Points + Ratings & Comments

A new first-level page, **Library Synchronization**, with today's "Sync
Cue Points" moving under it and a new **Ratings & Comments** subpage
beside it. Same job, same machinery, different payload: take one track
that several catalogs on the stick each know about, and make them agree.

## Why this is mostly a re-use exercise

The sync code was written generic and has stayed that way. Concretely:

- `domain::SyncMatch` holds two whole `Track`s named `trackA`/`trackB`,
  not by format, and says so in its own comment. **Reusable unchanged.**
- `domain::TrackMatcher::match()` pairs tracks by resolved file path
  first, falling back to title+artist+duration. Nothing about it is
  cue-specific. **Reusable unchanged.**
- `SyncController::analyze()` already runs all three catalog pairs,
  handles per-catalog mtimes, playlist and search scoping, staged edits,
  backup and undo, and hands results to
  `CrossSourceConflictDetector` for the case two pairs target the same
  third catalog. **All reusable.**

Exactly one thing is cue-typed: `domain::SyncPlan`, whose payload is
`std::vector<CuePoint> cuesToApply` and whose `Kind` is named around
cues (`NoCues`, `AlreadyConsistent`, …).

### How to share it

**Do not** template `SyncPlan` over a payload -- it crosses into the Qt
list models, and a template there buys nothing but obscurity.

**Do** extract the decision itself, which is the part worth not
duplicating and the part that would silently drift if copied:

```
// domain/sync_decision.hpp
enum class SyncSide { Neither, AOnly, BOnly, BothAgree, BothDiffer };
SyncSide compare(bool hasA, bool hasB, bool equal);
SyncPlan::Direction resolve(SyncSide, mtimeA, mtimeB);   // last-write-wins
```

Then `CueSyncPlanner` and `MetadataSyncPlanner` are each ~20 lines over
that shared rule, with their own plan struct and their own payload.
`CrossSourceConflictDetector` needs the same treatment: its logic is
"two pairs target the same third track", which is payload-independent.

## The gate: what can actually be written

The play-count lesson applies directly -- check writability *before*
promising a sync. Measured against the current code:

| | rating read | rating write | comment read | comment write |
|---|---|---|---|---|
| rekordbox `export.pdb` | yes | **no** -- but a 1-byte field, and `PdbRowWriter` already does in-place fixed-field overwrites | yes | **yes**, already |
| Engine `m.db` | yes | **yes** -- `djinterop::track::set_rating()` | yes | **yes** -- `set_comment()` |
| OneLibrary `exportLibrary.db` | **no** | no | **no** | no |

Two things follow.

**rekordbox↔Engine comment sync works with the code that exists today.**
Nothing new is needed on the write side at all.

**OneLibrary is the real work**, and it is reader work before it is
writer work: `onelibrary_reader.cpp` reads title, artist, bpm, length,
path, filename, bitrate, fileSize, key, play count and artwork -- no
track rating, no track comment. (Its only `comment` is a *cue* comment.)
The columns have to be found in `exportLibrary.db` and confirmed against
a real edit before anything can be synced into it.

**Rating into `export.pdb`** sits in between: one byte at a known offset
in a row `PdbRowWriter` already rewrites in place. Smaller than the
comment write that already ships, since comment is variable-length and
needed byte-budget fitting.

## Navigation

```
Stick
├── Browse Library
├── Library Synchronization        <- new first-level card
│   ├── Cue Points                 <- today's "Sync Cue Points", moved
│   └── Ratings & Comments         <- new
├── Housekeeping
│   └── Duplicate Tracks -> …
└── Backups -> …
```

Follow `DuplicatesHubPage`/`BackupsHubPage` exactly: a hub page fanning
out to subpages, each subpage navigated to deliberately. Nothing about
this is novel.

One existing bug to fix while moving the card: "Sync Cue Points" is
gated on `hasRekordbox && hasEngine`, which hides it on a stick that has
rekordbox and OneLibrary but no Engine -- a real configuration, and one
the analyze() code below the card already handles. The hub should be
enabled whenever **any two** catalogs are present.

## Does this help deduplication? Mostly no, and that matters

The tempting story is: two copies disagree on rating, so send the DJ to
Ratings & Comments, sync, come back and the group cleans up. **Measured
on RV2, that story is wrong**, and shipping it would send people to a
page that cannot help them.

Both groups still flagged by `hasUnpreservableDataAtRisk` look like this:

```
Moritz Hofbauer - Busy Ants (Olympe Remix)
   rekordbox   rating=-    SAME FILE as survivor   <- survivor
   engine      rating=1    DIFFERENT FILE
   onelibrary  rating=-    SAME FILE as survivor
```

The rating is on an Engine row for a **different physical file**. Sync
matches the *same* file across catalogs; it has nothing to say about two
different files. Syncing would change nothing here.

### What would actually fix these two

Propagate rating and comment onto the survivor the same way
`DuplicateCleanupPlanner` already propagates bpm, key and artwork: fill
a gap when the survivor has no value and exactly one other copy does.
In both RV2 cases exactly one copy carries a rating and the survivor
carries none -- an unambiguous fill with nothing lost, no human needed.
That would resolve both groups and take the cleanup from 630 to all 632.

It needs a rating/comment write path per format, so it inherits the
table above: Engine today, rekordbox comment today, rekordbox rating
cheap, OneLibrary blocked on reader work.

### What to say in the meantime

The general rule, on the Clean Up page, and true regardless of any of
the above:

> Deduplication only removes a file when nothing it carries would be
> lost. Where copies disagree on ratings or comments, that group is left
> for you. Synchronizing metadata first resolves more of them.

No link, until there is a page that would actually resolve the case in
front of the reader.

## Stray File Audit: read-only in v1

Decided: the audit page ships in v1 **read-only** -- it reports, it
never deletes. Content and layout come from
`docs/deduplication-roadmap.md`.

Two ways in, both from the Clean Up page's own explanation of what is
going on:

1. **The space diagram itself is clickable.** `SpaceReclaimBar` already
   shows reclaimable space against real capacity; clicking it opens the
   audit behind those numbers. Needs a cursor change and a hover
   affordance so it does not look inert -- a bar that is a link must
   look like one.
2. **A button beneath it**, for everyone who does not guess that a chart
   is clickable. Same destination. This is the discoverable path; the
   clickable diagram is the fast one.

Read-only means no survivor picking, no deletion, no staging: the tier
breakdown, the counterpart check, the per-rule outcomes, and which
catalogs were consulted. Everything it needs is already computed by
`walkAudioFiles` + `findUnreferencedFiles` + `DuplicateCleanupPlanner`.

## Order of work

1. **Hub page + move Cue Points under it.** Pure navigation, no logic.
   Fixes the two-catalog gating bug on the way.
2. **Extract the shared decision** (`sync_decision.hpp`), re-express
   `SyncPlanner` on top of it. No behaviour change; the existing sync
   tests are the safety net.
3. **Ratings & Comments, rekordbox↔Engine only.** Comment needs no new
   writer; rating needs the one-byte pdb write. Ships useful.
4. **OneLibrary rating/comment reader**, verified against a real edit
   (same snapshot-and-diff method as `docs/ratings-comments-plan.md`),
   then its writer, then it joins the pair loop.
5. **Gap-fill rating/comment in `DuplicateCleanupPlanner`**, which needs
   step 3 or 4 to have somewhere to write.
6. **Stray File Audit, read-only**, with both entry points.

Steps 1-3 are self-contained and shippable on their own. Step 4 is the
one with unknowns in it and should not block the rest.

## Not this feature

`docs/ratings-comments-plan.md` is a different thing and stays separate:
it is about rekordbox's `master.db` on the DJ's *computer* as a source of
truth, which is a scope expansion this project has deliberately not
taken. Everything here is stick-local, between catalogs already on the
stick, and needs none of that.
