# Deduplication: what ships, and what waits

Scope notes taken while building the unreferenced-file cleanup
(`unreferenced-file-cleanup-plan.md`). Everything here is either a
decision already made or a deliberate deferral -- the point is that
neither gets rediscovered from scratch later.

## Stray File Audit (v2, possibly a v1 preview)

The measurement page produced for RV2 works as a feature, not just as a
one-off report. Given a stick it can answer, without anyone reading a
log: how much space the files no catalog references are taking, what
each of them duplicates, how confident that claim is, and what a cleanup
would actually remove.

The shape that worked, as a template:

1. **A capacity meter drawn to scale.** "8.66 GB" says nothing; the same
   figure as a block on a stick with 1.5 GB free says all of it. This is
   now `SpaceReclaimBar.qml`, already on Clean Up Duplicates.
2. **Tiers by confidence, not one number.** 598 byte-identical, 28
   lower-quality copies, 6 not proven identical. A DJ treats those three
   groups differently and the page should not average them into one.
3. **The counterpart check, stated explicitly.** For every stray file:
   is there another file, still catalogued and still on disk, that it
   duplicates? Zero exceptions is a strong claim and worth showing.
4. **What the rules would do, per rule.** A table of what each safety
   rule holds back, including the rules that hold back nothing -- a rule
   that never fires on this stick is information, not filler.
5. **Say which catalogs were consulted.** A partial answer that looks
   complete is the failure mode this whole area has.
6. **Name the measurements that were thrown away.** The `ffmpeg` audio
   comparison produced provably false results and was discarded; a page
   that hides that is less trustworthy, not more.

**Why not v1:** it is a reporting surface over machinery that is still
being built, and the deletion path it would report on is only just
correct. A v1 preview showing items 1-3 read-only, with no action
buttons, would be cheap and safe if it earns its place.

## rekordbox's two formats disagree (v2 for repair; report only, if anything, in v1)

`export.pdb` and `exportLibrary.db` are the same rekordbox library in two
formats, not two libraries. They do not agree. On RV2:

- 290 files OneLibrary lists that `export.pdb` does not. **All 290 exist
  on disk.**
- 193 paths inside OneLibrary carried by more than one row.
- Containment on this stick is strict: rekordbox ⊂ OneLibrary ⊂ Engine.

**Detection is nearly free** -- it is a set difference over data every
scan already loads, and Library Health is the obvious home for it.

**Repair is not, and should not be attempted yet**, for a reason that
has nothing to do with effort: *we do not know that divergence is
wrong.* rekordbox may write `exportLibrary.db` from a different rule
than `export.pdb` (a plausible guess is playlist membership, untested).
Reporting "your library is inconsistent" about something the DJ's own
software does on purpose is worse than silence, and offering to "repair"
it could mean writing rows into `export.pdb` -- a write path that does
not exist (`PdbRowWriter` removes and repoints rows, it does not add
them).

**Order of work, if picked up:** find out *why* they differ first, on
two or three real sticks. Only then decide whether it is a health issue
at all. The 193 duplicate rows inside OneLibrary are the more clearly
wrong signal of the two and a better starting point.

**Already safe without any of this:** the deletion gate consults every
catalog present, so the divergence cannot cost anyone a file. That was
the urgent part and it is done.

## Play counts (decided, partly deferred)

Decided: a play count belongs to the application that kept it. rekordbox
keeps a running total, Engine keeps only a last-played timestamp; they
are not the same measurement and combining them across library types is
not a thing that can be done correctly. Cleanup no longer treats a
play-count disagreement as data at risk, and the Clean Up page explains
that it does not carry them over.

Deferred: **within one library type, merging copies should add the
counts up.** That is the right answer and is not implemented, because no
writer in this project can write a play count into any of the three
formats. It needs a write path first. Until then the counts go with the
copies that are removed, which the page says out loud.
