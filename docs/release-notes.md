# Release notes

Rippl is a fork of EchoPod (github.com/BasilSkyWalk/echopod), starting from EchoPod 0.12.0; EchoPod's own release notes are in its repository.

## Unreleased

- The equalizer's presets are files: `.txt` presets in `/EQ` (Equalizer APO's parametric format, as AutoEQ exports it) are listed after Off and Custom and play on the player, and the built-in Retro, Bass, Heavy, Pop, Jazz and Unique are gone; a saved built-in preset becomes Off. MP3, AAC and Opus are equalized on the second processor like FLAC, and changing tracks no longer leaves old audio in the buffer (from EchoPod pull request 56).
- After an update installs, the player restarts by itself once USB is unplugged, and the install percentage always has its own line, so it shows in portrait in every language.
- The library is built into segment files in `/.rippl/library`, which grow with the music instead of a fixed 4 MB `library.db` and 24 MB `build.tmp`, and a rescan reads only files that are new or changed. Resume and Up Next keep their songs across rescans. Covers move from the fixed 80 MB `covers.bin` into 4 MiB segment files with no album limit, and favorites use 64-bit album keys, so favorites made before this start over. Tide replaces rippl-sync: `tide sync` writes the same segment files as the player, so a synced card needs no rescan and the next rescan reads only new files, and `tide bookmarks` lists audiobook bookmarks.
