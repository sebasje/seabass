# Ideas for features

Here are some ideas which could be implememted in Seabass. No guarantees, this is NOT a roadmap, just a record of brainstorming.

## Loops

Show / display loops on tracks

## Developer Settings category:

- Prune/obfuscate my own usb stick data and add it as test dataset, create a testsuite which tests our functionality against these datasets

- Allows submitting usb stick libraries to upstream developer to add to testing harness, after obfuscation (track titles, artist names, etc.); tell transparently what data is submitted, and that no personal data or useful tracks/playlist are being submitted, it helps making seabass work with YOUR USB stick though, for very little effort. (Mention that we do not EVER phone home without explicit permission, and that privacy is a core feature. Recipient is sebas@kde.org)

- Add version, show it in --help and on About page, current version is 0.5 (beta)
- Add "Seabass is beta software. While we take utmost care with your data, operation is with inherent risks. Seabass creates backups, but you do use it at your own risk. Make sure you operate on backed-up data. Also: we strongly advice to test your stick on target hardware before your gigs and to always carry a backup."
- Developer credits


## USB Stick statistics:
    - stick filesystem (indicate which devices are supported with this FS, and where the limitations are (e.g. vfat, may not work on XDJ-RX or somesuch, max file size, etc.)
    - number of tracks and playlists, cue points etc. (per library type), number of tracks per key, number of track with rating, comments, BPM distribution, distribution of formats, tracks from streaming services including which one, ...
    - Used/free space plus nice diagram of where the diskspace goes (metadata per library, coverart, files, may dig down into files per playlist / artist, bit like KDE's filelight)
    - Speed measurements: reading speed, invent a score for read-speed (tracks / raw data and db, and save it locally per stick so we can compare stick performance at least for this laptop); make sure to note which USB protocol is used (USB-A or -C usually and keep stats for both)
    
## Adding metadata

- genre(s) from online sources
- which additional metadata can we find?
    
## Playlist creation / editing / removal

- Adding tracks to playlist from library view, including "New playlist, should remember last chosen one"
- Reordering playlists
- Editing playlist metadata
- Removal (with and without orphaned track deletion (i.e. tracks not in another list))
- Show / filter on tracks not currently in any playlist
- Filter on tracks with same key, within key range, within BPM range of N%, genre

## Transcode playlist

- Playlist as FLAC or other format with limited support: transcode to mp3@320 or somesuch so it can play on older hardware (create new playlist, sync cues and metadata), recognize these playlist in cleanup and duplicate, allow cleaning up but disable this by default

## Formatting USB sticks

Allow formatting (with all the usual warnings and safeguards); pick format based on hardware support, also allow adding artwork to a stick to easily recognize.

## Full stick backups (incl data) 

- Creating backups of a stick (either to / from local disk or directly from one USB device to another)
