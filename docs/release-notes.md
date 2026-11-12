# Release notes

Rippl is a fork of EchoPod (github.com/BasilSkyWalk/echopod), starting from EchoPod 0.12.0; EchoPod's own release notes are in its repository.

## Unreleased

The first Rippl release, forked from EchoPod 0.12.0.

### Before you update

- **The player's files move from `/.echo` to `/.rippl`.** The first start sets the card up again: settings, the equalizer, resume, Up Next, favorites, bookmarks, Bluetooth devices, music folder choices and the library start fresh, and the card is scanned. Old `/.echo` files are left alone and can be deleted.
- **The firmware is an 8 MiB image, `RIPPL.IMG`.** From EchoPod, install `flasher/HIFIEC90.IMG` first, since EchoPod only accepts 5 MiB images; from FiiO's firmware, rename `RIPPL.IMG` to `HIFIEC90.IMG`. `docs/device-install.md` has the steps.
- **Only English and Simplified Chinese are built in.** If you use another language, put its pack on the internal drive first (`tide languages install <code>`, or `Languages/<code>.bin` from `rippl-languages.zip`); without it the player starts in English.

### Library

- The library lives in segment files in `/.rippl/library`, which grow with the music instead of a fixed 4 MB `library.db`, 24 MB `build.tmp` and 80 MB `covers.bin`. Tracks, albums, folders and playlists are counted in 32 bits, so a library is no longer limited to 65,535 tracks; the builder is tested with 250,000.
- A rescan reads only files that are new or changed. Resume and Up Next keep their songs across rescans, and covers are kept for albums that didn't change, in 4 MiB segments with no album limit.
- Tracks whose path on the card is up to 1024 bytes play. Before, a path over 256 bytes, which deep folders with long Chinese or Japanese names reach quickly since each character takes 3 bytes, was in the library but wouldn't play.
- **Tide** replaces rippl-sync. `tide sync` writes the same segment files as the player, so a synced card needs no rescan; `tide bookmarks` lists audiobook and podcast bookmarks; `tide languages` installs language packs.
- Folders list their tracks by disc, track number and file name again, as before EchoPod 0.12.0, instead of by file name alone.

### Sound

- Volume has 85 levels of exactly 1 dB each, from 84 dB below the top on both jacks. Bluetooth headphones get the level the 3.5 mm jack would at the same volume, so Settings → Sound → Bluetooth level is gone.
- Settings → Sound → Filter names the DAC's five filters instead of numbering them, and **DRE** turns the DACs' Dynamic Range Enhancement Off, On or On with a lower threshold.
- The equalizer's presets are files: `.txt` presets in `/EQ` (Equalizer APO's parametric format, as AutoEQ exports it) are listed after Off, and the built-in presets and the 7-band Custom equalizer are gone; with no preset in `/EQ`, Settings has no Equalizer row. MP3, AAC and Opus are equalized on the second processor like FLAC, and changing tracks no longer leaves old audio in the buffer (from EchoPod pull request 56). WAV is decoded and equalized there too, and the main core no longer decodes FLAC or WAV itself: after an update, nothing plays until the player restarts, which it does by itself once USB is unplugged. The main core doesn't equalize either: switching the equalizer on during a track restarts the second processor at full speed where the track was, with a short gap, instead of the main core equalizing the rest of the track.
- 352.8 kHz (DXD) and 384 kHz WAV files play, at their own rate. The equalizer is off for them, as above 96 kHz.
- 32 kHz files play at 32 kHz, and 8 to 24 kHz files are raised to 44.1 or 48 kHz by a proper 64-tap filter on the second processor instead of drawing straight lines between samples, which dulled their top end and let copies of it through. The equalizer then works at the raised rate, so a preset sounds the same as on other music.
- A preset's OFF filters are skipped without checking their fields, so a preset with 10 active filters and OFF lines after them loads instead of saying a line is out of range.
- 32-bit WAV goes to the DAC with all 32 bits instead of being rounded to 24, and 32-bit FLAC and ALAC, which were refused, play the same way up to 96 kHz. The equalizer is off for 32-bit tracks, as for 176.4 and 192 kHz.

### Look

- The start-up screen shows the Rippl logo fading in, instead of the word Rippl.
- The Now Playing faces fill the whole screen, without the header and status bar, and grow into the room. In landscape the cassette's window is taller and the Turntable's record is bigger, with the song beside it at the top and the codec on its own line. In portrait the cassette gets a tape counter that counts turns of the take-up reel, and the Turntable is turned so the tonearm sits on top. The Turntable redraws 20 times a second while it plays, so its label turns smoothly at 16⅔ RPM, and its tonearm creeps in across the whole album or playlist instead of each song (it stays at the start with Shuffle songs, and crosses each album with Shuffle albums). The cassette's tape winds from one reel to the other the same way, across the whole album or playlist.
- The VU meters and LCD deck faces are gone: their needles and spectrum moved in a fixed pattern and never followed the music. Now Playing offers Standard, Cassette and Turntable.
- Lyrics stay on Standard Now Playing: a face no longer swaps the current lyric line in for the album or the artist.
- The Turntable face shows the album cover as the record's label again, as before EchoPod 0.12.0 printed it across the whole record, and the label turns with the record in place of the turning glint; without a cover the label is red with the Rippl logo on it.

### Buttons

- In both portraits the volume buttons turn the volume the other way, while holding them still skips and seeks as in landscape. **Swap volume buttons** now reverses only the volume, not lists, skips or seeking.

### Clock

- **Daylight saving:** Settings → System → Daylight saving moves the clock an hour in summer by itself, for Europe, US & Canada, Australia or New Zealand, or by the rule Tide reads from the computer's time zone (From computer). The time zone setting is now the standard-time zone and the clock chip keeps standard time; changing the setting keeps the time shown. Tide's `clock.bin` now carries the computer's standard offset and its rule.

### Languages and text

- Language packs other than English and Chinese load from `Languages/<code>.bin` on the internal drive, and Settings → Language lists the built-in two plus the packs it finds. A pack that is missing, too big or damaged is refused and the player starts in English.
- The 69 strings that were still English are translated in every language, and awkward or clashing Chinese strings are fixed.
- Track, artist and album names in GBK and JIS X 0213 characters beyond the common CJK set draw too.
- The big letter shown while scrolling fast comes from a 64 px font instead of the 16 px title font blown up 4 times, so it's sharp instead of blurred.

### Errors

- `/.rippl/errors.log` lists the files the player couldn't open or use (tracks, presets, language packs, its own files and those of the library and the scan), with the time and the reason, including a track whose path is too long to play. The player makes the file at the first error and only adds to it, so delete it to empty it.

### Updates

- The install percentage always has its own line, so it shows in portrait in every language, and after an update installs the player restarts by itself once USB is unplugged.

### Memory

- The language pack moves to 16 KB of RAM that is free once the player has started, so a pack can be 16 KB instead of 12, and the glyph cache grows from 34 to 46 KB.
- About 10 KB of code (the Bluetooth protocol and the Bluetooth, Equalizer, Music folders and Date & time screens) loads after start into a second code area, leaving about 21 KB free in the main one. If it fails to load, those screens say to reinstall the update and Bluetooth stays off.

### Removed

- The listen history: the player no longer logs plays or writes `.scrobbler.log`, and Settings → Library → Listen history, rippl-sync's `--history` and `tools/rippl_history.py` are gone. Existing `history.bin`, `history-exported.txt` and `.scrobbler.log` files can be deleted.
