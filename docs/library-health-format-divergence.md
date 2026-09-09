# Library Health: repairing a divergent rekordbox / OneLibrary pair

## What this is for

`export.pdb` and `exportLibrary.db` are one library written in two formats,
not two catalogs. Every Seabass write now keeps them level -- Sync mirrors,
and so do Add Cue, Local Cue restore and stray-cue removal -- and Sync
deliberately does not plan them as a pair, because the mirror makes them
identical by construction (see `sync_controller.cpp`).

That fixes the future and does nothing for a library that arrived crooked.
On a real stick, OneLibrary referenced **290 files** `export.pdb` did not.
On the committed fixture the split is starker: **1161 rekordbox tracks,
1644 OneLibrary tracks, and every rekordbox track appears in OneLibrary** --
so 483 rows exist in one half only, all in the same direction.

Nothing repairs that today. This is where it belongs: Library Health is
already where cross-catalog disagreement is reported, already scans all
three catalogs (`m_pendingScanFormats` includes `onelibrary`), and already
stages repairs that are backed up and undoable.

## Why the existing checker cannot do it as it stands

`LibraryConsistencyChecker` is per-catalog on purpose, and says so:

> a healthy rekordbox copy says nothing about whether a broken Engine row
> is safe to repair

That is right for Engine, and it is exactly wrong for OneLibrary -- which
is not an independent catalog but the same library in a newer format, so
each half genuinely can vouch for the other. Any work here must be scoped
to that one pair and carry that reasoning, or it reintroduces the bug the
existing comment prevents.

## The three shapes of divergence

**1. Both halves have the track; one has cues, the other does not.**
Unambiguous: copy across. The common leftover from writes that predate the
mirror.

**2. Both halves have the track and their cues genuinely differ.** A
conflict. Merge where the union is unambiguous, refuse otherwise -- the
stance `DuplicateCueConsolidator` already takes. Never arbitrate by mtime:
both files sit on the same stick and rekordbox writes both, so "newer"
does not mean "more correct".

**3. The row exists in one half only.** Two causes that look identical on
disk -- the track was added on one side, or removed on the other -- and
nothing on the stick distinguishes them. So this one asks.

## The dialog for shape 3 (decided 2026-09-09)

Per track, or per selection:

1. **"Track was added to <A>, so add it to <B> as well"** -- preselected,
   because added is the common case.
2. **"Track was removed from <B>, so remove it from <A> as well"**
3. **Ignore, don't touch**

## What each option actually costs, which is not evenly distributed

| Option | rekordbox side | OneLibrary side | Buildable today |
|---|---|---|---|
| 2. remove | `PdbRowWriter::removeTrack` (clears the presence bit) | `removeTrackByPath` | **yes** |
| 3. ignore | -- | -- | **yes** |
| 1. add | nothing exists | nothing exists | **no** |

Neither format has an "insert a track" capability. `PdbRowWriter` can clear
presence bits and overwrite existing fields in place; it cannot add a row.
`OneLibraryCueWriter` can write cues, remove rows and propagate missing
fields into an existing row; it cannot create one.

**So the option chosen as the default is the only one that cannot be
built yet**, and the measured direction points it at the harder of the two:
every divergent row on the fixture, and the 290 on RV2, exists in
OneLibrary and not in rekordbox -- so "add it to the other half" means
inserting a row into `export.pdb`, a proprietary binary format with
fixed-length fields, page structure and indices. Inserting into OneLibrary
is a SQL INSERT into a schema Seabass only partly understands (the colour
mapping is already documented as unverified); inserting into `export.pdb`
is a different order of problem.

There is one precedent for creating catalog rows --
`libdjinterop_engine_library_creator` builds a whole Engine library -- but
it does it through libdjinterop, a maintained library that knows the Engine
schema. Nothing equivalent exists for pdb or OneLibrary.

## Proposed sequencing

**Step 1 -- see it.** Detect and report all three shapes, repair none.
Cheap, immediately useful, and it turns "OneLibrary has 483 more rows than
rekordbox" from a number in a test into something a user can look at. The
comparison is the reusable part and is worth having before any repair
exists.

**Step 2 -- shapes 1 and 2.** Cue divergence, repaired with the writers
that already exist, staged through `RepairIssueChange`'s machinery. Reuses
`Repairable` / `Conflict` unchanged.

**Step 3 -- shape 3, options 2 and 3.** Removal and ignore. Option 1 is
shown but disabled, with the reason stated in the dialog rather than
hidden: Seabass can remove a row from either half and cannot yet add one.
A default that silently does nothing would be worse than an honest
disabled option.

**Step 4 -- option 1.** Row insertion, as its own piece of work, and
biggest first: `export.pdb` is where the demand is. This is Seabass
becoming an exporter rather than an editor, and deserves to be planned as
that rather than smuggled in as a repair.

## Where it plugs in

- **Comparison:** new, and the only genuinely new domain logic. Identity is
  by file path, which works now that `toContentPath()` trims the
  space-padding `export.pdb` puts on fixed-length fields.
- **Reading:** `readAllStickCatalogs()` already does it.
- **Issue model:** `LibraryConsistencyIssue` needs a kind for this, or a
  flag naming which half is the survivor. The existing kinds already
  express shapes 1 and 2.
- **Repair:** `RepairIssueChange` already stages a cue write plus a row
  removal, backs up first and is undoable. The one new thing it must learn
  is writing to the *other* half than the one the issue was found in --
  every existing repair writes to the catalog it scanned.
- **Backups:** unchanged. `filesToBackup()` names both halves already.

## What would make this wrong

- Arbitrating by mtime.
- Letting the comparison run against Engine. It is not the same library,
  and the per-catalog rule exists for it.
- Offering "add to the other half" as a working default before insertion
  exists.
