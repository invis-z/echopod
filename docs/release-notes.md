# Release notes

Rippl is a fork of EchoPod (github.com/BasilSkyWalk/echopod), starting from EchoPod 0.11.1; EchoPod's own release notes are in its repository.

## Unreleased

- The language pack moves to the 16 KB of RAM after the data area, which is free once the player has started (a full read-back test passed on a player), so a pack can be up to 16 KB instead of 12; the glyph cache takes the 12 KB it leaves and grows from 34 to 46 KB.
- The listen history is gone: the player no longer logs plays to `/.rippl/history.bin` or writes `.scrobbler.log` when it connects, Settings → Library → Listen history and Tide's `--history` are removed, and `tools/rippl_history.py` is deleted. Existing `history.bin`, `history-exported.txt` and `.scrobbler.log` files are left on the card and can be deleted.
- The equalizer's presets are files: `.txt` presets in `/EQ` (Equalizer APO's parametric format, as AutoEQ exports it) are listed after Off and Custom and play on the player, and the built-in Retro, Bass, Heavy, Pop, Jazz and Unique are gone; a saved built-in preset becomes Off. MP3, AAC and Opus are equalized on the second processor like FLAC, and changing tracks no longer leaves old audio in the buffer (from EchoPod pull request 56).
- After an update installs, the player restarts by itself once USB is unplugged, and the install percentage always has its own line, so it shows in portrait in every language.
- The library is built into segment files in `/.rippl/library`, which grow with the music instead of a fixed 4 MB `library.db` and 24 MB `build.tmp`, and a rescan reads only files that are new or changed. Resume and Up Next keep their songs across rescans. Covers move from the fixed 80 MB `covers.bin` into 4 MiB segment files with no album limit, and favorites use 64-bit album keys, so favorites made before this start over. Tide replaces rippl-sync: `tide sync` writes the same segment files as the player, so a synced card needs no rescan and the next rescan reads only new files, and `tide bookmarks` lists audiobook bookmarks.
