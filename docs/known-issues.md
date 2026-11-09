# Rippl known issues

Rippl is early firmware made by one person and tested on one player. It comes with no warranty: use it at your own risk. Read this before you install, and keep a way back (see "If something goes wrong" in `docs/user-guide.md`).

## Safety

- **Volume:** Rippl's top volume is close to FiiO's maximum, and sensitive earphones can be very loud. Turn the volume down before you put them in.
- **Battery:** Rippl switches off when the battery runs low, but doesn't yet refuse to start on an almost empty battery. If it switches off by itself, charge it before using it again.
- **Bricking:** custom firmware has left Echo Mini players unable to start during development (all were recovered through maskrom). Rippl's updater writes one firmware slot at a time and reads every chunk back, but a power cut during an install is still a risk: install with the battery charged.

## Installing

- **Tested on one player.** One Echo Mini 8 GB, starting from FiiO firmware 3.7.0, 3.9.0 and 4.0.0, installed from Windows 11. Other units, other FiiO versions and other computers haven't been tried.
- **Installing from a Mac is unproven.** Earlier attempts to copy an image from a Mac with FiiO's firmware running were refused (FiiO's firmware showed "Updating" for under a second and kept itself). The cause isn't known. Install from Windows if you can.
- **The 512 MB Echo Mini isn't supported.** Rippl refuses its firmware, and its firmware ignores Rippl's file.
- **Going back to FiiO's firmware** is checked with FiiO's 8 GB 3.7.0, 3.8.0, 3.9.0 and 4.0.0 images, and 0.3.0 installed FiiO 4.0.0 on the player and was installed back from it (Rippl 0.2.0 refuses 4.0.0, see `docs/user-guide.md`). Another custom firmware can't be installed from Rippl: go back to FiiO's first.
- **If the player doesn't start at all,** the only recovery known is the chip's maskrom mode (`docs/device-recovery.md`). It works, but it needs a patched tool and care.

## USB

- **The first connection to a new computer can fail.** Windows may report "Device Descriptor Request Failed", or the drives may drop off. Unplug and plug in again; the second try has always worked.
- **Eject both drives before you unplug.** Unplugging first can lose the last changes, and the player warns you ("Unplugged before ejecting"). Once, a file deleted from Windows and ejected properly was still on internal storage afterwards; it hasn't happened again.
- **The SD card can be marked as needing repair.** Twice a computer reported the card's exFAT volume as not cleanly closed. Nothing was lost. If Windows offers to scan and fix the card, let it (or run `chkdsk /f` on its drive letter; on a Mac, Disk Utility's First Aid).
- **Speed on a Mac hasn't been measured.** On Windows 11, copying runs at about 3.7 MB/s to the SD card and 5 MB/s to internal storage, and reading from the SD card at about 3.9 MB/s.
- **USB DAC mode is tested on one player with Windows 11** (44.1 and 48 kHz, 16 and 24-bit) **and an Android phone.** Linux and a Mac haven't been tried. If it doesn't appear as a sound card, crackles, or plays at the wrong speed, set Settings → USB mode back to Storage and report what you saw. Known limits: full speed only (44.1 or 48 kHz, 16 or 24-bit; 88.2 and 96 kHz don't fit this chip's USB buffer); about 20 ms of delay; the player stays in DAC mode until you unplug it or hold Back; Power doesn't turn the screen off during DAC mode. If the computer stops sending audio and starts again, the first 20 ms are silent. Pressing the volume buttons during playback used to crackle and wobble the pitch for a moment (reported with 0.10.0); 0.11 applies the volume in software so no USB packet is lost, and the counters on the DAC screen show whether any was. The media keys and the plug-in prompt haven't been tried on a player yet.
- **macOS leaves `._` files** beside everything it copies. Rippl ignores them, but they take space. `dot_clean` on the drive removes them.

## Music and library

- **Gapless MP3** needs the LAME tag that LAME and ffmpeg write. MP3s without one (some iTunes and older encoders) keep a short gap between tracks. After a seek, or when playback resumes part-way through a track, that track's silent end isn't trimmed, so the next track starts a few tens of milliseconds late.
- **Characters:** song, artist and album names show Latin letters (Latin-1, Latin Extended-A, Romanian and Vietnamese letters), Greek, Cyrillic, Korean (the 2,350 common Hangul syllables), Simplified Chinese (GB 2312), Traditional Chinese (the 5,401 common characters of Big5 level 1), Japanese kana and kanji (JIS X 0208), and CJK punctuation. Rarer Hangul syllables, the rarer Traditional Chinese characters (Big5 level 2), emoji and other scripts (Arabic, Hebrew, Thai, Devanagari…) show as `?`. Chinese characters shared with Japanese are drawn in their Simplified Chinese style. Tested on the player with Korean, Chinese, Japanese and accented titles; Greek, Cyrillic and the extended Latin and Vietnamese letters are covered by tests and the simulator but haven't been checked on the player yet; the first time a screen shows a character, it's read from internal storage.
- **Formats:** FLAC, ALAC, AAC, Opus, QOA, MP3 and WAV play, at 32, 44.1, 48, 88.2, 96, 176.4 and 192 kHz, and WAV also at 352.8 and 384 kHz (the 352.8 kHz clock uses a PLL setting stock never used, and neither rate's speed has been measured). FLAC and ALAC also play at 352.8 and 384 kHz, with up to 24 bits. Files at 8, 11.025, 12, 16, 22.05 and 24 kHz also play: the second processor raises them 2, 3, 4 or 6 times to 44.1 or 48 kHz with a 64-tap filter that keeps everything up to 0.41 of the file's rate within 0.01 dB and the copies above it at least 90 dB down, and the equalizer then works at the raised rate. APE and the other formats FiiO lists appear in the library but don't play; Rippl says so. ALAC, AAC and Opus play cleanly on the player tested. Every format is decoded on core 1, so after an update nothing plays until the player restarts. ALAC files with more than two channels don't play. 32-bit FLAC and ALAC play up to 96 kHz; above that they're refused, as decoding them can outrun the second processor (32-bit WAV plays at every rate). Only AAC-LC plays: HE-AAC and HE-AAC v2 files that say so in their header are refused, and ones that don't play their core layer, which sounds duller. AAC with more than two channels doesn't play. Opus (`.opus`) plays mono and stereo files; files with more channels, packets longer than 60 ms (rare: encoders default to 20 ms) and Ogg files with Vorbis audio don't play. The volume adjustment some tools store in an Opus header is ignored, and a file made of several chained streams stops at the end of the first. AAC, Opus, 32-bit FLAC and ALAC, and FLAC and ALAC above 96 kHz run core 1 at 200 MHz, twice the clock the other formats use, which may cost some battery (not measured yet); if one of them stutters, the `c` figure on Diagnostics → Frame time shows how busy core 1 is. Right after an Opus seek the first tenth of a second differs very slightly from playing straight through, as Opus decoders settle. QOA (`.qoa`) plays mono and stereo files on the player; files with more channels don't play. FLAC plays with blocks of up to 16,384 samples, the largest FLAC's subset allows (ffmpeg writes 8,192 at 88.2 and 96 kHz and 16,384 at 176.4 and 192 kHz); larger blocks don't play.
- **WebM isn't supported** (`.webm`, `.weba`, `.mka`). `ffmpeg -i in.webm -c copy out.opus` turns a WebM file with Opus audio into an Opus file Rippl plays, without re-encoding.
- **Building the library takes time.** On the first start, after music changes over USB, and once after updating to 0.2.0 (the library format changed). A 1,563-track, 144-album card takes about 1 minute 45 seconds: 20 s for the tracks and 1.4 minutes for the covers, where decoding each album's picture takes about half a second. Later rescans keep the covers whose art didn't change and decode only new or changed albums; the first rescan after updating still decodes them all. Covers from a cover file on a card that isn't FAT32 or exFAT are always decoded again. The player is busy until it finishes: let it finish before you plug it in. Tide can build the library on a computer instead.
- **Library size:** the library now grows in 1 MiB segment files and isn't limited to 65,535 tracks; it's tested on a computer with 250,000 tracks but not yet on the player, where the earlier format was tested with 6,558 tracks. Segments the scan used for scratch stay reserved on the card for the next rescan: a small library takes about 8 MB.
- **Album art:** covers grow in 4 MiB segment files of 133 covers, with no limit but the card; when the card is full, new albums show without art. Covers that are replaced or removed take up room until a quarter of the covers are such leftovers; the next rescan then copies the rest into fresh segments. Tested on a computer, not yet on the player.
- **Cover Flow** orders albums by album artist, then year, not by album title like the Albums list.
- **Chapters inside one file** (.m4b or .m4a) are read from the `chpl` chapter list only, up to 255 per file (the player places the first 160 chapters of a book, a book with more shows none), to the second, and without their names. A book folder with several files ignores them, the chapters of a file with QuickTime text-track chapters aren't read, and a file with a 64-bit atom size (over 4 GB) shows none. A library built before chapters were read shows none until the next rescan.
- **Spoken word is decided by the top folder.** Only tracks inside an `Audiobooks`, `Audiobook`, `Podcasts` or `Podcast` folder at the top of the card count as audiobooks or podcasts; the same folder names deeper down are music. An author with music too appears under Artists with the music only.

## Themes

- **The new themes are checked in the simulator only.** Fjord, Dusk, Neon, Phosphor, Paper, Olive and High contrast have not been seen on the player's panel. Their contrast was measured on the colour values, not on the glass, and the darkest tints (Phosphor's green-black, Neon's glow) may look crushed there.
- **The cover panel on Home stays dark on the light themes** (Light, Paper and Olive), so it shows as a dark panel on a light screen. Cover Flow does the same.
- **Dark and Light keep a few low-contrast details** that the new themes avoid, because they are kept exactly as they were: white text on the top of the selection bar (3.1:1 with the Blue accent), the disconnected-Bluetooth icon on Dark's header (2.3:1), the Now Playing scrub fill against its track on Dark (2.3:1), and on Light the charging bolt at the top of the header (2.9:1), the equalizer's band readout while editing (2.9:1) and the scrollbar thumb (2.7:1). High contrast has none of these.
- **Going back to an older Rippl after choosing a new theme resets all settings.** Older firmware rejects a theme value it doesn't know and starts from the defaults, so the whole settings file is ignored, not only the theme. Choose Dark or Light before you go back.

## Now Playing faces

- **The battery cost of the faces' animation hasn't been measured.** A face redraws while a track plays, about ten frames a second; Standard redraws far less.
- **Standard shows if the face code can't be loaded.** The faces are read from internal storage when first needed. If that fails (internal storage doesn't answer, or the copy is damaged twice), Now Playing draws Standard and keeps doing so until you restart the player.
- **Going back to an older Rippl resets the face to Standard.** Older firmware ignores the setting and writes 0 in its place the next time it saves. No other setting is lost.
- **The faces' region has 5,528 of 13,824 bytes free**, so a bigger new face needs something else in the region made smaller, or a bigger region. `.high_ram` keeps 31,140 bytes free (44,964 before the faces).
- **"Standard" isn't translated** in any language; the Portrait status bar setting shares the word.

## Languages

- **The translations are unreviewed.** No native speaker has checked them yet (the Chinese starts from a contributor's text). That is why only English and Chinese are built in; the other languages are optional packs. Terms such as the name of the Play key and Up Next are guesses in several languages.
- **Long texts may not fit.** The screen is 320 × 170 px, and some notices and hint lines in Japanese, Korean, German, Italian, Portuguese and Dutch run longer than the English; they may wrap onto an extra line or be cut. Not yet checked screen by screen on the player.
- **A language change needs a restart.** The Language row saves the choice, and the player uses it from its next start.
- **Some text stays English:** the Battery low notice shown before storage starts, the panic screen, the USB drive names the computer sees, the folder names `Audiobooks` and `Podcasts`, the fallback names the library builder writes (such as Unknown Artist), and the debug abbreviations in Diagnostics.

## Tide

- **New.** Tide replaces rippl-sync and writes the player's own segment files; the host tests check that a card it synced reads the same as one the player built, but it hasn't been run against a real card yet.
- **Tested on Windows 11 only.** The macOS (Apple silicon) and Linux files pass an automatic check on GitHub but haven't been run on a real card yet. On a Mac, the downloaded file needs `chmod +x` and its quarantine flag removed before it runs (see `docs/user-guide.md`).
- **FAT32 and exFAT only.** On other file systems the file times may not match the player's, and the player then rescans anyway.
- **Copies that never finished** (a file whose size on the card is larger than what was written) are read as the player sees them; recopy such files from the computer.

## Not there yet

- No ReplayGain. Lyrics come only from `.lrc` files next to the song (UTF-8, up to 3 KB of text and 160 lines); lyrics inside the music file aren't read.
- The equalizer is off for 176.4 and 192 kHz tracks, for 32-bit tracks and in USB DAC mode. Turning it on while a track plays restarts core 1 at full speed, which leaves a short gap in the music.
- Bluetooth pairing with a PIN or a six-digit number, LDAC or other codecs, and DSD/DSF files aren't supported.
- Bluetooth has been tested with one pair of headphones (OnePlus Buds 3). While music goes to Bluetooth the player can't read the headphone jack: pause, plug wired headphones in and press Play to switch.
- The 4.4 mm balanced output works (reported by a tester). Settings → Output can't switch sound between the jacks: both are driven by the same DACs, so it only picks the volume table (see `docs/user-guide.md`).
- Audiobooks and podcasts: the shelves, covers and keeping them out of Music are checked on a player; bookmarks, in-order play and the 30 s / 15 s jumps so far only in the simulator. There's no playback speed control.
- The brightness, Favorites stutter, sound-after-USB and battery-while-charging fixes since 0.1.0 still need a tester's confirmation on a player.
