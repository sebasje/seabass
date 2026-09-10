# Metadata Backup: the DJ's own work, kept off the stick

## Why

Everything a DJ adds to a track lives only on the stick: the cues they set
in the booth, the rating they gave it at 3am, the comment reminding them
what it mixes into. The audio is replaceable -- it can be re-imported from
anywhere. The annotations cannot. They exist in exactly one place, on a
device that gets reformatted, re-exported, dropped and lost.

Seabass already has a narrow version of this (`LocalCueStore`, the "Local
Cue Backup" card, currently marked "Needs rework"). It backs up cues and
nothing else, keys its database off an XDG path rather than `~/Seabass`,
and its restore path merges cue-by-cue with no way for a user to say
"just take the backup's word for it". This is that feature, reworked and
widened to the whole of what a DJ authors.

Two cards, because pulling and pushing are two different decisions taken
at different times:

- **Metadata Backup** -- read every catalog on the stick, store what the
  DJ added, locally.
- **Restore Metadata** -- put it back on a stick that has lost it.

## What gets stored, and what does not

The rule: **store what a DJ authored, plus just enough of what the
software wrote to identify the track again.** Not a second copy of the
library.

| Stored | Why |
|---|---|
| cues and loops (position, kind, hot number, colour, comment) | the whole point |
| rating | authored, and lost on re-import |
| comment | authored |
| play count / last played | authored by playing; the only record of it |
| playlist membership | authored, and cheap to store |
| title, artist, filename, duration, bpm, key | identity, and what a browse view has to show |
| cover art | asked for explicitly, and it makes the browse view legible |
| stick-relative path | the strongest matching key there is |

Deliberately **not** stored: waveforms, beat grids, analysis files,
audio. All of it is derived from the audio file, all of it is large, and
none of it is the DJ's work. `docs/anonymized-export.md` draws the same
line for the same reason.

Cover art is content-addressed: the image is copied to
`~/Seabass/metadata/artwork/<sha256>.<ext>` and the row stores the hash.
A library where 1500 tracks share 300 album covers stores 300 files. The
hash is over the file bytes, so re-running a backup never writes an image
twice.

## Where it lives

```
~/Seabass/metadata/metadata.db          the store
~/Seabass/metadata/artwork/<sha>.jpg    cover art, content-addressed
```

`~/Seabass` is `paths::localRoot()`, which honours the user's preference
and `$SEABASS_HOME`, so a test run can never touch the real store. Both
paths come from `paths::localMetadataDir()`; nothing else invents a
location.

## Identity: how a stored track is found again

This is the part that decides whether the feature works, because a
restore onto a freshly written stick has to recognise tracks it has never
seen a row for.

**Artist, title and length -- the same rule `domain::matchTracks()` uses
everywhere else in Seabass.** The key is normalised artist + title; when
either is missing, the normalised filename stands in, exactly as
matchTracks falls back. Length is not part of the key, because two
readings of one file differ by rounding. It is a guard applied to the
candidates the key finds, with the same 2-second tolerance and the same
"only when both readings are real" rule: a zero duration means unreadable,
not a zero-length track, and gating on it would split one track into two
rows the moment one catalog failed to report a length.

Length earns its place on one real case: a radio edit and an extended mix
share an artist and a title, and without it one would silently inherit the
other's cues.

**Not the file path**, which was this document's first answer and was
wrong. A path is the strongest signal when both sides are looking at one
stick -- which is why `matchTracks` tries it first -- and the weakest
thing to key a store on. The whole point of this store is that it outlives
the stick: a re-export renames folders, a rebuilt library moves
`Contents/` around, and the same track bought again lands somewhere else
entirely. Artist and title travel with the recording; the path does not.

The stick-relative path is still stored, for display and for saying where
a track was last seen. Nothing matches on it.

The store is therefore keyed on the recording and not on which stick it
came from. A track is one row no matter how many sticks carry it; the
`library_id` and `stick_label` columns record where it was last seen, for
the browse view and for nothing else.

## Conflict policy

Both directions ask the same question and default differently, because
the safe answer is different.

| | default | reasoning |
|---|---|---|
| stick -> store (backup) | **overwrite all** | the stick is where the DJ works. A cue they set last night is newer than whatever is stored. |
| store -> stick (restore) | **skip all** | the stick may already have been re-cued. Never overwrite a DJ's live work from a backup. |

One choice per run, applied to every conflict in it -- "overwrite all" or
"skip all", not a per-track prompt. A per-track prompt over 1500 tracks
is not a decision anyone makes; it is a decision nobody finishes.

A conflict is per field group, not per track: cues, rating, and comment
each conflict independently, and the policy applies to each. A track
whose cues differ but whose rating is only present in the store gets its
cues resolved by the policy and its rating filled in either way -- filling
an empty field is not a conflict.

## Restore: what it offers, and how it writes

The restore card's headline case, from the request: **tracks on the stick
that have no cue points at all, for which the store has some.** That is
the unambiguous case, it is common after a re-export, and it needs no
conflict policy because there is nothing to overwrite.

Beyond it, the same page offers rating and comment, under the skip-all
default -- but not everywhere, and the difference is measured rather than
assumed (`tests/pdb_rating_write_test.cpp`):

| | rating | comment |
|---|---|---|
| Engine | yes, `set_rating` | yes, `set_comment` |
| OneLibrary | yes, plain SQL | yes, plain SQL |
| DeviceLibrary (`export.pdb`) | yes, a 1-byte field at offset 89 | **no** |

`export.pdb` keeps a comment in a `device_sql_string` whose byte span is
fixed at export time, and the only write available is a re-encode into
exactly that span. So a comment can be shortened or replaced with one
that fits, and never lengthened. On the committed fixture **1160 of 1161
tracks have no comment at all**, which is a span of zero bytes -- so
precisely the tracks a restore would want to give a comment back to are
the ones that cannot take one. Growing the row is the page-allocator
problem `docs/library-health-format-divergence.md` sizes up, not a field
overwrite.

One more thing the format cannot say: rekordbox stores "unrated" and
"zero stars" as the same byte, and the reader maps 0 to "no rating". So
writing a 0 clears a rating rather than setting a zero-star one. The
store keeps the two apart; DeviceLibrary cannot.

Writes are **staged, not applied**. Restore builds one `PendingChange`
per track into the library's `LibraryEditSession`, exactly as
`LocalCueController::applyRestore()` already does; the page's Save writes
them through `runSaveLoop`, which backs up first. Nothing here invents a
write path, and nothing here reaches the stick without the user pressing
Save.

## Browse

"A lightweight way to browse it": one page, one list, a search field over
title/artist/filename. Per row: cover art thumbnail, title, artist,
duration, rating, cue count, and the stick label it was last seen on.
Selecting a row shows its cues.

Lightweight means the list is fed by a SQL query with a LIMIT, not by
reading the whole store into memory and filtering in QML. A store that
has seen a few thousand tracks is small; one that has seen ten sticks
over a year is not.

## Schema (v1)

```sql
CREATE TABLE tracks (
  id INTEGER PRIMARY KEY,
  match_key TEXT NOT NULL,         -- normalised "artist|title", or "filename" as fallback
  relative_path TEXT NOT NULL,     -- where it was last seen, for display only
  filename TEXT NOT NULL,
  title TEXT, artist TEXT,
  duration_seconds REAL, bpm REAL, music_key TEXT,
  rating INTEGER,                  -- NULL means unrated, never 0
  comment TEXT,
  play_count INTEGER, last_played_at TEXT,
  artwork_sha TEXT,                -- -> artwork/<sha>.<ext>
  artwork_extension TEXT,
  library_id TEXT, stick_label TEXT,
  source_format TEXT,
  first_seen TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE cues (
  track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
  kind TEXT NOT NULL,              -- 'memory' | 'hot'
  hot_number INTEGER NOT NULL,
  position_ms REAL NOT NULL,
  color TEXT, comment TEXT,
  is_loop INTEGER NOT NULL, loop_end_ms REAL NOT NULL
);
CREATE TABLE playlists (
  track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
  name TEXT NOT NULL, position INTEGER NOT NULL
);
CREATE TABLE schema_version (version INTEGER NOT NULL);
```

`rating` is nullable and never written as 0 for "unrated" --
`domain::Track::rating` is an `optional<int>` for exactly this reason and
flattening it here would throw the distinction away on the way in.

## What the restore page says out loud

A page that silently declines to write a field teaches the DJ that the
field is unreliable. One that names the format and the reason teaches
them something true about their own library. So when a scan finds tracks
whose comment only DeviceLibrary catalogues, the page says so before the
save rather than the log saying so after it:

> N tracks' comments cannot be put back: they are catalogued only in
> DeviceLibrary, which stores a comment in a fixed space decided when the
> stick was exported and cannot make room for a new one. Their cues and
> ratings still go back. Engine and Device Library Plus take comments of
> any length.

## Order of work

1. `MetadataStore` plus its test: schema, upsert under both policies,
   `readAll()` as a `LibraryReader`, artwork intake. Qt-free.
2. `backUpMetadata()` use case: catalogs in, summary out, progress and
   cancellation through the existing ports.
3. `planMetadataRestore()` use case: store + stick tracks in, per-track
   proposals out. Pure, and the piece most worth testing directly.
4. `MetadataBackupController` and the backup card and page.
5. Browse page.
6. `MetadataRestoreController`, the restore card, staged writes.

## What would make this wrong

- Storing waveforms or analysis files. It is a metadata store; the moment
  it holds derived binary data it is a slow, partial stick backup.
- Keying tracks by path. The feature exists so metadata survives the
  stick, and a path is a fact about a stick.
- Writing to the stick outside the edit session. Every other write path
  in Seabass backs up first; this one does not get an exception.
- A per-track conflict prompt.
- Treating a missing field as a conflict. Filling in a blank is not
  overwriting.
- Letting the browse page read the whole store to show twenty rows.

## Relationship to Local Cue Backup

`LocalCueStore`, `LocalCueController`, `LocalCuePage.qml` and
`domain::LocalRestorePlanner` are the narrow ancestor of this and are
already marked deprecated in the UI. They should go once this ships --
pre-1.0, and two features that back up overlapping data to two different
databases is worse than either alone. That removal is deliberately not
part of this branch: it is a separate decision, and it wants the new path
proven on real sticks first.
