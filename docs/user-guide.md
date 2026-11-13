# Rippl user guide

Rippl is a replacement firmware for the Snowsky (FiiO) Echo Mini music player. It has a Cover Flow album browser, a music library built on the player, favorites, playlists and an Up Next queue. It is not made or supported by FiiO.

Read `docs/known-issues.md` before you install. Rippl has been tested on one player so far.

**Use it at your own risk.** Rippl comes with no warranty (see `LICENSE`). Replacing a player's firmware can leave it unable to start; during development that happened, and the player was brought back through maskrom (`docs/device-recovery.md`). Only install it if you're prepared to do that.

## Which players

- **Echo Mini 8 GB: supported.** Its FiiO update file is called `HIFIECnn.IMG`.
- **Echo Mini 512 MB: not supported.** Its FiiO update file is called `MINIVnnn.IMG`. Its firmware ignores Rippl's file, and Rippl refuses its firmware.

Tested starting points: FiiO firmware 3.7.0, 3.9.0 and 4.0.0 (Settings → About on the player shows yours).

## Before you install

1. Copy anything you want to keep off the player's internal storage.
2. Charge the battery.
3. Download `HIFIEC90.IMG` and `SHA256SUMS.txt` from the [Rippl releases](https://github.com/invis-z/rippl/releases), and check the file:
   - Windows (PowerShell): `Get-FileHash HIFIEC90.IMG`
   - macOS or Linux: `shasum -a 256 HIFIEC90.IMG`

   The result must match the line for `HIFIEC90.IMG` in `SHA256SUMS.txt`. If it doesn't, download it again.

## Install

1. Connect the player to your computer. The internal storage appears as a drive called ECHO MINI.
2. Copy `HIFIEC90.IMG` to the top level of that drive, not into a folder. Delete any other `HIFIECnn.IMG` or `ECHOMINI.IMG` file there first: the player installs the one with the highest number.
3. Eject the drive (Windows: Safely Remove; macOS: Eject), then unplug.
4. Take the microSD card out.
5. Switch the player off and on again. It shows "Updating, Don't power off" for about a minute or two. Leave it alone until Rippl starts.
6. Put the microSD card back. On the first start with a card, Rippl builds its library and album covers, which takes a minute or two (about 1 minute 45 seconds for 1,563 tracks and 144 albums).

If "Updating" flashes for under a second and FiiO's firmware comes back, the install didn't happen. See "If something goes wrong" below. One tester whose FiiO firmware kept refusing the file got it to install after first updating to FiiO's 3.8 firmware.

## Your music

- Put music on a microSD card formatted FAT32 or exFAT, in any folder layout.
- Rippl plays FLAC, ALAC (Apple Lossless `.m4a`), AAC (`.m4a`), Opus (`.opus`), QOA (`.qoa`), MP3 and WAV, from 8 to 192 kHz, and FLAC, ALAC and WAV also at 352.8 and 384 kHz, FLAC and ALAC with up to 24 bits. 32-bit FLAC and ALAC play up to 96 kHz. HE-AAC isn't supported yet. It lists the other formats FiiO lists, and says when it can't play one.
- Album covers come from art inside one of each album's first four tracks (Ogg and Opus pictures included), or from a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks.
- `.m3u` and `.m3u8` playlists anywhere on the card appear under Music → Playlists.
- Libraries of many thousands of tracks work; `docs/known-issues.md` lists the sizes tested so far.
- Rippl keeps its own files in a hidden `/.rippl` folder on the card: about 13 MB for a small library, growing with the music and the covers. Don't delete it unless you want the library rebuilt.
- A card is needed: the library, your settings, favorites and resume points are all saved there. Without one, Home shows "No microSD card" and a changed setting isn't kept ("Insert a microSD card to save settings"). Put a card in and restart the player.
- Music can also go on the player's internal storage (the drive called ECHO MINI, about 8 GB on the 8 GB model). The library always holds the music of both: albums, artists, genres, songs and playlists mix the two, and under Folders the internal storage is one more folder, **Internal storage**, next to the card's own folders. Firmware images (`HIFIECnn.IMG`) and other files that aren't music or playlists are ignored. An SD card is still needed, because the library and Rippl's other files stay in `/.rippl` on the card; the player never writes to internal storage. To leave internal storage's music out, untick its folders in Settings → Library → Music folders (below). Audiobooks and podcasts on internal storage play as music: the Audiobooks and Podcasts shelves only read the card's top-level `Audiobooks` and `Podcasts` folders.
- If the same album is on the card and on internal storage, both copies are in the library, and with the same album and album artist they show as one album with every track twice. Untick one copy's folder in Settings → Library → Music folders to keep only the other.

### Music folders

Settings → Library → **Music folders** lists the top-level folders of the card and of internal storage, each with a tick: all of them are ticked at first. ▶ unticks a folder (or ticks it again); leaving the screen with Back saves the choice and rebuilds the library without the unticked folders and everything in them, so their albums don't appear in Albums, Artists, Cover Flow, Folders, playlists or All Songs. The row in Settings shows **All**, or how many folders are ticked, for example **3 of 5**.

- Music directly in the card's root stays in the library; only folders are listed.
- The `Audiobooks`/`Audiobook` and `Podcasts`/`Podcast` folders aren't listed: the Audiobooks and Podcasts shelves always read them.
- Hidden folders (a name starting with `.`, or the hidden attribute) and `System Volume Information` aren't listed.
- The internal storage's top-level folders follow the card's, as **Internal storage/**name. Music directly in the internal storage's root, like music in the card's root, isn't listed and stays in the library.
- Up to 32 folders are listed (fewer if their names are very long: the screen keeps about 500 bytes of names). Folders beyond that aren't listed and stay in the library.
- The choice is kept by folder name in `/.rippl/folders.bin` on the card, so a folder you add later is ticked, and a renamed folder counts as a new one. A folder that is gone keeps its choice for when it comes back.
- Tide reads the same file and leaves the same folders out (see Build the library on a computer).

## Copying music over USB

1. Connect the player. Two drives appear: internal storage and the SD card.
2. Copy music to the SD card or to internal storage.
3. **Eject both drives before you unplug.** If you unplug first, recent changes may be lost, and the player warns you.
4. When music changed, on the card or on internal storage, the player rebuilds its library by itself. Copying other files doesn't trigger a rebuild. Settings → Library → Rescan library rebuilds it on demand. A rebuild reads only new or changed files, and keeps your place: the song you were playing comes back paused where you left it (in its album, if the list you played it from changed), and Up Next keeps its songs, minus any that are gone.

During a long copy the screen goes dark after 30 seconds; any button lights it again. If the drives disappear from the computer on their own while the cable stays in, the player connects again by itself (without asking Storage or DAC) and the computer may warn that a drive was removed without ejecting. Afterwards, Home → Diagnostics → **USB errors** shows what happened in the last session, or *None*:

- `E1/2`: commands that failed on internal storage / on the SD card.
- `r3`: SD card transfers that failed and were tried again; `i1`: times the card had to be started again (also when it didn't answer at the start of the session, for example after it was taken out and put back while the player was on).
- `s4:A208`: the card controller's step and status at the last card failure (for bug reports).
- `b1`: times the computer reset the connection after it was working.
- `v2`: moments the player briefly lost the cable's power reading and carried on.
- `FS`: the connection dropped from high speed to full speed in this session (the computer sees one disconnect); `fs`: it was already at full speed from an earlier session since the player was switched on.

## Using the player as a USB DAC

**Untested on a player so far** (see `docs/known-issues.md`). In this mode the player is a sound card for your computer or phone: their audio plays through the player's headphone outputs.

1. On the player, Settings → System → USB mode → DAC.
2. Plug in the USB cable. The player shows "USB DAC". No driver is needed on Windows 10 and later, macOS, Linux, Android or iOS (phones need a USB-C or Lightning adapter that passes data).
3. On the computer, choose Rippl as the sound output (Windows: the speaker icon in the taskbar; macOS: System Settings → Sound → Output). The player shows the sample rate in large digits with the bit depth below it, for example 48 kHz and 24-bit.
4. Use the computer's volume, the player's volume keys, or both. DAC mode has its own volume, separate from music: it starts at 60 of 85, the screen shows it as a bar like Now Playing's, and the player remembers it for next time. Start low. The volume buttons work as on Now Playing: a tap changes the volume, and right after a tap holding the rocker keeps changing it; your Swap volume buttons, Volume buttons and orientation settings apply.
5. The player's buttons control the computer's player, like a headset: ▶ is play/pause, holding + or − for a second is next or previous (or a tap, with Volume buttons set to *Tap: skip, hold: volume*), and Menu is next when it's set to Next track. Whether the app reacts depends on it, as with a keyboard's media keys.
6. Unplug when you're done, or hold Back for a second to go back to the player's own music (it keeps charging).

The status bar shows the battery as on the other screens. Hold Menu to open a stats panel at the bottom of the screen, and hold it again to close it; it updates every second. *missed* counts audio packets that never arrived, *dropped* and *repeated* the samples the player removed or added to stay in step, and *restarts* the times the output had to start over; *starts*, *stops* and *formats* count the output starting, stopping and the computer changing format, *retries* the player re-sending its clock to the computer (Hi-Res DAC only), *control* the computer's control messages, *wrong slot* packets that came in the wrong frame, *late irq* waits of over half a millisecond for the USB interrupt, and *loss* the last and longest runs of missed packets. *missed* and *restarts* should stay at 0 while the music plays. In DAC mode the clocks of the computer and the player drift apart slowly, so *dropped* or *repeated* grows by up to a few hundred a minute, in short bursts, and that's normal; in Hi-Res DAC mode both should stay near 0. If *missed* goes up while the music plays, please report the panel.

The player accepts 16 or 24-bit at 44.1 and 48 kHz; the computer picks one (on Windows: Sound settings → Rippl → Properties → Advanced). **Hi-Res DAC** mode (USB mode → Hi-Res DAC) also takes 88.2, 96, 176.4, 192, 352.8 and 384 kHz at 16, 24 or 32 bits; it needs a USB 2.0 high-speed port (on a full-speed port it stops at 96 kHz), and it keeps the player's clock in step with the computer's instead of now and then dropping or repeating a sample. It works with Windows 10 (version 1703) and later, macOS, Linux and iOS. The volume is applied in the player's software while it's a DAC (the chip's own volume stays at the top), so very low levels keep a little less resolution than music does. Music on the player stops while it's a DAC, and your drives don't appear: set USB mode back to Storage to copy music. The setting takes effect the next time you plug in, unless **Ask when connected** is on (the default): then plugging into a computer asks *Storage*, *DAC*, *Hi-Res DAC* or *Charge only* each time, with the saved mode selected, and goes with the saved mode after 5 seconds or when you press Back. *Charge only* keeps playing and charging without connecting. A charger never asks.

## Build the library on a computer

Rebuilding on the player takes a minute or two (1,563 tracks and 144 albums take about 1 minute 45 seconds). A computer can build the same library and album covers instead, straight onto the card, and tell the player that the card has already been scanned. The player then keeps them, and when you add music later it only reads the new files.

The tool is called **Tide**. Download it from the release page, under Assets; it's one file, with nothing to install:

| Computer | File |
|---|---|
| Windows | `tide-windows-x64.exe` |
| Mac with Apple silicon (M1 and later) | `tide-macos-arm64` |
| Linux (x86-64) | `tide-linux-x64` |

**Windows**

1. Copy your music to the card, in the player over USB or in a card reader.
2. Double-click `tide-windows-x64.exe`. Windows may say it "protected your PC" because the file isn't signed: click **More info**, then **Run anyway**.
3. It finds a card the player has used and asks "Card to sync [E:\]": press Enter. For another card, type its drive letter (or drag the drive onto the window) and press Enter.
4. When it says "Done", press Enter to close it, eject the card (in the player: both drives), and put it back in the player or unplug.

From a command prompt, `tide-windows-x64.exe sync E:` does the same without asking. `tide sync` is what runs when you give no command.

**Mac**

1. Copy your music to the card.
2. In Terminal, allow the downloaded file to run once: `chmod +x ~/Downloads/tide-macos-arm64 && xattr -d com.apple.quarantine ~/Downloads/tide-macos-arm64`.
3. Run `~/Downloads/tide-macos-arm64` and press Enter to use the card it finds, or drag the card from the desktop into the window first. You can also give it straight away: `~/Downloads/tide-macos-arm64 sync /Volumes/CARD`.
4. Eject the card.

**Linux**

`chmod +x tide-linux-x64`, then `./tide-linux-x64 sync /media/you/CARD` (the card's mount point), then eject the card.

**Intel Macs, other computers, or from the source.** Install Python 3, then the tools' packages once with `python3 -m pip install -r tools/requirements.txt`, and run `python3 tools/tide sync /Volumes/CARD`. On Windows, `tools\tide.bat sync E:` finds Python for you (double-clicking it asks for the card).

It reads the whole card, as the player does, and writes the library, the album covers and a record of every music file into the card's `/.rippl` folder, in the same segment files the player uses (`/.rippl/library`, `/.rippl/covers` and `manifest.bin`), plus `scan.bin`. If the player has never used the card, it asks before creating `/.rippl` (`--yes` creates it without asking). It writes only to segment files the player isn't using and switches over at the very end, so stopping it half-way leaves the player's own library as it was. Covers that are already on the card and whose art didn't change are kept without decoding. It then prints what it found, for example "1,402 tracks, 120 albums, 3 playlists" and "115 covers (110 kept), 5 without art".

**Music folders.** Tide reads Settings → Library → Music folders from the card (`/.rippl/folders.bin`) and leaves the same top-level folders out of the library and the fingerprint, saying how many ("Leaving out 2 top-level folders"). Changes inside a left-out folder don't make the player rescan.

**Internal storage.** The player's library also holds the music on internal storage, so Tide looks for it too: with the card in the connected player, it finds the internal storage by its drive name, RIPPL, and prints both drives it reads ("Tide sync: E:\ (exFAT)" and "and the internal storage F:\ (FAT32)"). The drive comes named ECHO MINI: rename it to RIPPL once on your computer (in Windows, right-click it and choose Rename; on a Mac, rename it in Finder), or give it each time with `--internal`: `tide-windows-x64.exe sync E: --internal F:` (on a Mac, for example `--internal "/Volumes/MY PLAYER"`). If it isn't found (for example with the card in a card reader), Tide says so, builds the library from the card only, and the player rescans after the sync to add internal storage's music.

**Why the player doesn't rescan.** After a USB session the player checks for changes: it compares the names, sizes and modification times of the music, playlists and cover images on the card with a fingerprint it keeps in `scan.bin`. The computer writes the same fingerprint, so the check finds nothing new and the player keeps the computer's library. With a card reader there's no USB session, so the player uses the new library straight away.

**When the player rescans anyway.** If the fingerprints differ, the player rescans as it would without the computer, and nothing is lost. That happens when:

- the music changed after the computer ran;
- the card is exFAT and a file's time was saved without a time-zone tag (modern Windows, macOS and Linux always add one; some cameras and old devices don't);
- the card is FAT32 and the computer is a Mac or runs Linux: how these read FAT32 file times hasn't been checked against the player yet;
- the card has folders more than 31 levels deep or paths longer than 1,023 bytes (the player can't fingerprint those);
- a name was saved decomposed (for example by Linux) and read on a Mac, or only as an old DOS short name with accented letters.

To see whether it works on your card, let the player scan it once, then run Tide with `--check` without changing anything (for example `tide-windows-x64.exe sync E: --check`). "They match" means the player won't rescan after the next sync.

**After a firmware update** the player checks the music once when it first starts ("Checking for changes"), and rescans only if the music changed, or if it can't read the library on the card (as after updating to 0.2.0, which changed the library format). `scan.bin` remembers the firmware version; the computer keeps it. A `scan.bin` the computer had to create has no version yet, so the player checks once on its next start too.

| Option | Effect |
|---|---|
| `--dry-run` | Read everything and say what would be written, but write nothing |
| `--no-covers` | Build the library and keep the covers the card already has without looking for art, so new albums show without covers until the next rescan on the player |
| `--no-fingerprint` | Leave `scan.bin` alone, so the player compares the music with its own last scan |
| `--internal DRIVE` | The player's internal storage drive, when it isn't found by its name, RIPPL (see above) |
| `--no-clock` | Don't write `clock.bin`, so the player keeps its own time (see Clock below) |
| `--check` | Only compare the computer's fingerprint with the player's |
| `--yes` | Create `/.rippl` without asking |
| `--verbose` | List the albums without art |

The computer picks each album's cover the way the player does: the picture inside the first of the album's first four tracks that has one (FLAC, MP3, WAV, DSF/DFF, `.m4a`, `.ape`, `.wma`, `.ogg` and `.opus`; a front cover wins), otherwise a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks. So several albums can share one folder and still each get their own embedded art. The covers can look a little different from the player's own, because the computer resizes them with a different filter. When the player rescans later (for example after you copy a new album to it), it keeps the computer's covers and only decodes art that's new or changed; on a card that isn't FAT32 or exFAT, albums whose cover is an image file are decoded again.

## Buttons

| Button | In lists | On Now Playing |
|---|---|---|
| Volume up / down | Next / previous item; hold to scroll faster | Volume; hold Vol+ for the next track, Vol− for the previous one (in audiobooks and podcasts: 30 s forward, 15 s back) |
| Play | Select; a song plays the list from that song. Hold on an album for Favorites, hold on a song for Play Next / Add to Up Next | Play / pause; hold to seek |
| Menu | Hold for Settings | Opens Up Next; hold for the Equalizer, when `/EQ` has presets (or, with Settings → Buttons → Menu button → Next track: next track; hold for Up Next) |
| Back | Back; hold to go home | Back; hold to go home |
| Power | Screen off or on; hold 2 s to turn off | Same |

With the screen off, ▶‖ still pauses and resumes, a tap of vol± changes the volume and holding vol± skips tracks (or jumps 30 s / 15 s in audiobooks and podcasts). After 30 s paused the player powers its audio output down to save battery, so resuming after a longer pause takes a moment longer. With the screen off, FLAC, ALAC, WAV and MP3 at 48 kHz or less play with the processor at a lower speed; switching between the two speeds happens at a track change, which then has a short gap instead of following seamlessly (Diagnostics → Savings → No 96 MHz turns this off).

Settings → Buttons → **Volume buttons** changes what the rocker does on Now Playing and with the screen off: *Tap: volume, hold: skip* (the default), *Tap: skip, hold: volume*, or *Tap: volume, hold: seek* (5 s steps while held). Right after you tap the volume, holding the rocker keeps changing the volume in every mode. **Swap volume buttons** turns the volume direction around: + turns it down and − turns it up. Skipping, seeking and moving through lists stay as they are.

At the top of an artist's, album artist's or genre's album list, **Play all** plays every song there in album order and **Shuffle all** plays them shuffled. All Songs and playlists have **Shuffle all**. The list still opens on the first album or song, so scroll up to reach them.

## Settings

Choose Settings on Home, or hold ≡ anywhere except on Now Playing. The top of the list holds the settings you change most: **Shuffle**, **Repeat**, **Bluetooth** (see Bluetooth headphones), **Equalizer** (see Equalizer) and **Sleep timer**. The rest are in five groups: ▶ opens a group, and Back goes back to Settings. Each row shows its current value, and a change applies as soon as you pick it.

- **Sound:** **Gain** (Low or High), **Output** (Auto, 3.5 mm or 4.4 mm), **Filter**, **Balance**, **DRE** (the DACs' dynamic range enhancement: Off, On, or On, lower threshold; changing it restarts the DACs, so playback skips briefly).
- **Display:** **Theme** picks the look of menus, lists, headers and Now Playing:
  - **Dark:** the default, light text on near-black, with a blue bar.
  - **Light:** dark text on a light background.
  - **Fjord:** Nordic slate with a frosted ice-blue bar and dark text.
  - **Dusk:** deep indigo with a magenta-to-violet header and a cyan bar.
  - **Neon:** black with a hot-pink outline on the selected row and a magenta glow in the header.
  - **Phosphor:** a green terminal; the selected row is an inverse block.
  - **Paper:** warm cream and sepia ink, with a kraft header and an oxblood bar.
  - **Olive:** a mid-tone, reflective handheld-LCD look with a dark block for the selected row.
  - **High contrast:** black and white with a yellow block and a grey header.
  - **Accent color** changes the selection bar on Dark and Light only; the other themes keep their own colours, and the row is hidden for them. **Highlight** (gradient or flat) applies to themes with a gradient bar (Dark, Light, Fjord, Dusk and Paper) and is hidden on the rest. Choosing another theme keeps your accent and highlight for when you come back. Cover Flow and the cover panel on Home stay dark on every theme.
  - **Now Playing** picks how the Now Playing screen looks:
    - **Standard:** the default, in the theme's colours.
    - **Cassette:** a tape deck with turning reels whose tape winds from the left reel to the right one across the whole album or playlist, each song an equal share (with Shuffle songs it stays on the left reel; with Shuffle albums it crosses each album); the tape across the head opening is the song's progress bar (portrait adds a J-card and a tape counter that counts turns of the take-up reel, so it runs faster at the start of a song than at the end).
    - **Turntable:** a record that turns at 16⅔ RPM, with the album cover as its label turning with it (the Rippl logo on a red label when there's no cover), and a tonearm that creeps in across the whole album or playlist, each song an equal share, like a record side (with Shuffle songs it stays at the start; with Shuffle albums it crosses each album) (in portrait the record player is turned so the arm sits on top).
    - **Tape Window:** the screen is the player's clear window, looking at a cassette at the size it would be in a Walkman scaled to the player, near the top to leave room for the heads. The song, the artist and album, the track number and the time are written on the label's top strip; through the label's window you see the hubs turning on the player's spindles and the tape winding from the left reel to the right one across the whole album or playlist, like the Cassette's. In portrait the picture stays where it is and turns with the player, as a cassette in a turned player would, so its writing runs along the long side.

    The faces use their own colours, so Theme, Accent color and Highlight don't apply to them, and a face fills the whole screen: the header and status bar are hidden while it shows. They move only while a track plays, and stand still while it's paused. Lyrics show on Standard only; a face never shows them. The seek hint, the volume and the skip, play and pause badge appear on every face. With nothing playing you see Standard's message.
  - **Brightness**, **Screen timeout**, **Orientation** (see Portrait mode). **Animations** makes screen slides and Cover Flow scrolling Slower (1.5× as long) or Faster (0.6×). **Battery percent** hides the number next to the battery. **Portrait status bar** → Large makes the status bar's icons and text bigger when the screen is in portrait (it has no effect in landscape). **Clock** shows the time in the status bar (see Clock).
- **Library:** **Music folders** chooses which top-level folders are in the library (see Your music) and **Rescan library** rebuilds it. **Start screen** picks what the player opens on: Main menu (Home), Now Playing, or one of the Music lists (Albums, Artists, Favorites, Playlists, Cover Flow, Album Artists, Genres, Folders or All Songs). **Cover Flow** shows 5 covers (the centre one and two each side) or 3 (one each side), and **Cover Flow order** sorts it by album artist or by album title. **Artist albums** → Cover Flow opens an artist, album artist or genre as a Cover Flow of just their albums. **Folders & playlists** → Cover Flow opens a playlist as a Cover Flow of its albums and a folder of folders as a Cover Flow of those folders (below). **Lyrics** shows or hides lyrics on Now Playing (see Lyrics).
- **Buttons:** **Volume buttons** and **Swap volume buttons** (see Buttons). **Menu button** makes ≡ on Now Playing open Up Next (the default) or skip to the next track. **Power on** → Hold makes the player start only when Power is held for about a second; a shorter press switches it straight back off, so a tap in a pocket or bag doesn't turn it on. With a cable plugged in it always starts, and RST always starts it.
- **System:** **Language** (see Language), **Date & time** sets the clock and **Daylight saving** moves it an hour in summer (see Clock), **Auto off** turns the player off after 1, 5 or 10 minutes without a key press (never while music plays or a cable gives power), **USB mode** (Storage, DAC or Hi-Res DAC) and **Ask when connected** choose what plugging into a computer does (see Using the player as a USB DAC).

With **Folders & playlists** set to Cover Flow, a playlist opens as a Cover Flow of its albums in playlist order: songs next to each other from the same album make one tile, and an album that comes back later in the playlist gets a tile again. ▶ on a tile opens the whole album. Music → Folders, and any folder that holds only folders, opens as a Cover Flow of its folders, each showing the cover of the album of its first song (the folder's own songs first, otherwise its first folder's, and so on down). ▶ on a folder that again holds only folders moves into it, and Back moves out again; a folder with songs opens as a list. A folder's own picture (`folder.jpg`) isn't used yet. Audiobooks and Podcasts stay lists.

## Lyrics

Put a `.lrc` file next to a song with the same name (`01 Come Together.flac` and `01 Come Together.lrc`; `.LRC` works too), and Now Playing shows the lyrics: in landscape under the title, in place of the artist, album and format lines; in portrait in place of the cover. Synced lyrics (lines starting with `[mm:ss.xx]`) scroll with the song, the current line in bold in the middle, and a current line too long for the screen scrolls sideways. A line can carry several times (`[00:12.00][01:30.00]Chorus`), `[offset:+500]` shows every line half a second earlier (`-` later), and word times such as `<00:12.50>` are skipped. A file without times shows as plain text that moves down the song as it plays. The lyrics are hidden while you seek (hold ▶).

The file must be UTF-8 (with or without a BOM); other encodings, such as the GBK some Chinese sites use, show garbled. Lyrics show in every script the player draws (see Characters in `docs/known-issues.md`). The player reads at most 3 KB of lyric text and 160 lines per song (about two to three times a typical song); anything past that is left out. Lyrics stored inside the music file (ID3 `USLT`/`SYLT`, a Vorbis `LYRICS` tag) aren't read. **Settings → Library → Lyrics → Off** hides them.

## Clock

The player has a clock chip that keeps time while it's off. Rippl reads it when it starts and every ten minutes, and sets it in two ways:

- **Tide** writes the computer's time, its standard time zone and its daylight saving rule to the card (`/.rippl/clock.bin`) at the end of every sync, and the player takes it when the USB session ends (eject the card safely, or unplug): a toast says "Clock set from the computer". The clock then matches the computer's to within the seconds between the end of the sync and the eject. A card synced in a card reader doesn't set the clock, because the player can't tell how long ago that was; sync with the player plugged in, or set the time by hand. `--no-clock` leaves the clock alone.
- **Settings → System → Date & time** sets it by hand: vol± choose a row (year, month, day, hour, minute, time zone), ▶ starts changing it (vol± change the value, ▶ finishes), Back leaves and sets the clock. Set the time the clock should show, and the time zone of standard (winter) time: with daylight saving on, UTC+01:00 for Berlin even in summer.

**Settings → System → Daylight saving** moves the clock forward an hour in summer and back in autumn by itself, at the right second: **Off**, **Europe** (the EU, the UK and their neighbours, all at 01:00 UTC), **US & Canada**, **Australia** (New South Wales, Victoria, Tasmania, the ACT and South Australia) or **New Zealand**. **From computer** follows the rule Tide read from the computer's own time zone, which also covers places the list doesn't name; a Tide sync chooses it (or Off, when the computer's zone has no daylight saving, or changes it in a way no yearly rule describes). Changing the setting keeps the time on the screen, and moves the time zone by the hour the setting adds or takes away. The clock chip keeps standard time, so Diagnostics → Clock is an hour behind the status bar in summer.

**Settings → Display → Clock** (Off by default) shows the time, `HH:MM`, in the status bar next to the play icon. In portrait, when the time and the battery percentage don't both fit, the percentage is left out. Until the clock has been set, the status bar shows no time and Date & time says "Not set". A clock that says it's earlier than the day before the firmware was built counts as not set too, so a chip that was never set properly doesn't show a wrong time.

**After installing a new firmware**, if the clock isn't set, the player asks once, after the start-up screen and the music check: "Set the date and time?" → **Set now** opens Date & time (starting from the date the firmware was built), **Later** (or Back) leaves it; it doesn't ask again until the next firmware. Syncing with Tide with the player plugged in sets it too. Diagnostics → Clock shows what the chip itself holds, with ` VL` while it has never been set.

Whether the chip keeps time on every player hasn't been confirmed yet; if yours loses the time when switched off, set it again or sync.

In Cover Flow (Music → Cover Flow), the tile before the first album, with a shuffle mark for a cover, is **Random album**: press ▶ on it to play a random album from its first track.

## Portrait mode

Settings → Display → Orientation has Landscape, Portrait and Portrait, flipped. The screen turns as soon as you pick, and the player starts the same way next time. Pick whichever portrait way round puts the buttons where you like them.

In portrait, Home shows the album art under the menu, Now Playing puts the cover above the track and Cover Flow scrolls albums from top to bottom. The volume rocker turns with the screen, so the button on top moves up through lists. In both portraits a tap turns the volume the other way from landscape: the button that turns it up in landscape turns it down. Holding the buttons skips to the next and previous track as in landscape, and seeking forward and back stays the same too.

## Language

English and 简体中文 are built in. Japanese, Korean, German, French, Spanish, Italian, Portuguese, Dutch, Swedish, Danish, Norwegian, Finnish, Russian, Ukrainian, Polish and Indonesian are language packs you add to the player's internal drive (the one named RIPPL): `tide languages install de` copies the German pack (`tide languages` lists them), or copy `<code>.bin` from the release's `rippl-languages.zip` into a `Languages` folder at the root of that drive. Settings → System → Language lists the built-in two and every pack it finds there. Pick one and the player says "Restart to apply"; hold Power to turn it off and on again, and every menu, setting and message uses that language. A pack that is missing or damaged when the player starts is skipped, and the player starts in English. Song, artist and album names are never translated.

The translations were not checked by native speakers, and a few long texts may be cut short on the small screen. Folder names stay as they are: audiobooks and podcasts still go in folders named `Audiobooks` and `Podcasts`.

## Equalizer

Open it from Settings → Equalizer, or hold ≡ on Now Playing. The equalizer plays presets from files, so with no preset on the card Settings has no Equalizer row, and holding ≡ on Now Playing says "No presets in /EQ".

- **Presets** are files: put up to eight `.txt` presets in an `EQ` folder on the card, in Equalizer APO's parametric format (AutoEQ's `ParametricEQ.txt` works as it is: a `Preamp:` line and up to ten `Filter` lines of type `PK`, `LSC` or `HSC`, gain ±12 dB, Q 0.1 to 10). They're listed after Off, by file name; files copied over USB show up when the USB session ends. Moving through the list plays each one, so you can hear them as you go. ↩ or ▶ keeps the one you're on. A file the player can't use stays in the list and says why (for example "Line 4: gain over 12 dB"); filters it can't play, such as low-pass, are skipped and counted.

The player remembers the preset you used last, by its file name, in `/.rippl/equalizer.bin`. If that preset file is gone at the next start, the equalizer is Off. The equalizer also applies to Bluetooth headphones. The second processor does the equalizing and runs at full speed while the equalizer is on, which costs a little battery. Switching the equalizer on in the middle of a track restarts the second processor at full speed, so the music stops for a moment and carries on where it was; switching it off takes effect straight away, and the second processor slows down again from the next track. It's off for 176.4 and 192 kHz tracks and for 32-bit WAV, which goes to the DAC with all 32 bits (the screen says so), and in USB DAC mode.

## Charging

The player charges whenever it's plugged in, on or off. Turning it off while it's plugged in (or plugging a charger into a switched-off player) shows the charge screen: the battery level, or "Charged" when it's full. The screen goes dark after 5 seconds; press any button to see it again. Hold Power for 2 seconds to turn the player on, or unplug it to switch it off completely. Plugging into a computer still opens USB mode.

## Bluetooth headphones

Settings → Bluetooth shows whether Bluetooth is on and which headphones are connected. Open it to turn Bluetooth on or off, connect, switch or pair headphones. The screen lists your headphones (the player remembers up to six, most recent first) with their state, a **Pair new device** row, and a hint for the selected row at the bottom.

**Pair new headphones:**

1. Put the headphones in pairing mode.
2. Select **Pair new device** and press ▶. The player searches and lists what it finds below that row.
3. Select your headphones and press ▶. The row says "Connecting…", then the player shows "Connected to …".

**Every day:** with Bluetooth on, the player connects to your most recent headphones by itself when it starts and whenever they come back in range. If a connection stalls, it resets and tries again within about 15 seconds. To switch to other remembered headphones, select them and press ▶. To disconnect, select the connected headphones and press ▶: they stay disconnected until you press ▶ on them again or restart the player. To forget headphones, hold ▶ on them.

Music plays through the headphones whenever nothing is plugged into either jack. To switch to wired headphones, pause, plug them in and press play: while music is going to Bluetooth the player can't see the jack. After you unplug, the next play goes back to Bluetooth. Each play or resume waits a moment (about half a second) before the music starts, so the headphones don't miss the opening.

**Volume:** the player sends Bluetooth headphones the same level the 3.5 mm jack would get at that volume, so the volume buttons cover the whole range. The headphones' own volume control is separate from the player's.

The headphones' controls work: pause, play and next/previous track, and taking them out of your ears pauses if they have in-ear detection. If they disconnect, playback pauses. With nothing connected for 5 minutes, Bluetooth switches itself off to save battery; it comes back when you open its screen, press play with nothing plugged in, or restart the player. A small Bluetooth mark next to the play icon at the top shows Bluetooth is on (dim) or connected (bright).

## Audiobooks and podcasts

Put audiobooks in a folder called `Audiobooks` and podcasts in a folder called `Podcasts`, at the top of the card (any capitalisation; `Audiobook` and `Podcast` work too). Home then shows **Audiobooks** and **Podcasts**; they stay hidden while those folders don't exist. Books and episodes stay out of Music (Cover Flow, Albums, Artists, Genres, Songs and shuffle), so they never turn up between your albums.

```
Audiobooks/
  The Long Road/          one folder per book
    01 - Departure.mp3    chapters play in file-name order
    02 - The River.mp3
  Short Story.mp3         a book in one file works too
  Novel.m4b               so does an .m4b with chapters
Podcasts/
  Tech Talk/              one folder per show
    2026-10-01 - Episode 41.mp3
```

- **Audiobooks** lists your books, each with "3 chapters", "Chapter 2 of 12" or "Finished" (for an .m4b, its own chapters). ▶ on a book carries on where you stopped (or starts a finished book again). Hold ▶ on a book to see its chapters.
- **Podcasts** lists your shows; open one to see its episodes, each with "23:10 left" or "Played".
- Every book and every episode remembers its own place, even when you play music in between or turn the player off. The places are saved in `/.rippl/bookmarks.bin` (the last 128 books and episodes).
- Books and shows always play in order: shuffle doesn't apply to them, and your shuffle setting for music stays as it was.
- On Now Playing, holding Vol+ jumps 30 s forward and holding Vol− jumps 15 s back. Jumping past the end of a chapter goes to the next one. To pick another chapter, use the book's chapter list.
- An episode or book counts as finished once it played to its last tenth (at most its last 30 s).
- These folders still appear under Music (Albums, Artists, Folders and so on), as all tracks on the card do. Give them a genre such as "Audiobook" if you want to find them there.
- An .m4b (or .m4a) with chapters shows "Chapter 3 of 13" on Now Playing, and the Menu button opens the chapter list with each chapter's start time: pick one to jump to it. Holding Vol+ and Vol− still jump 30 s and 15 s. The chapters are read from the file's chapter list (`chpl`); chapter names aren't shown.
- Playback speed can't be changed yet.
- To see on a computer which episodes and books you finished (for example to delete played podcasts in a sync script), run `tide bookmarks E:\ --finished` (or `python3 tools/tide bookmarks E:\ --finished` from the source) with the card's drive or mount folder. It prints one path per line. Without `--finished` it lists every bookmark with its position; `--json` prints them all as JSON.

## Updating Rippl

1. Copy the new `HIFIEC90.IMG` to the top level of internal storage.
2. Eject both drives. The player checks the file and shows "Installing update", then "Update installed".
3. Unplug: the player restarts by itself and starts the new version.

## Going back to FiiO's firmware

1. Download FiiO's firmware for the **8 GB** Echo Mini from FiiO's website and unzip it.
2. Delete `HIFIEC90.IMG` from internal storage if it's still there.
3. Copy FiiO's `HIFIECnn.IMG` (for example `HIFIEC40.IMG`) to the top level of internal storage, unchanged. Don't copy the `MINIVnnn.IMG` file from the same zip: that one is for the 512 MB model, and Rippl refuses it.
4. Eject both drives. The player checks the file (this takes a while) and shows "Installing FiiO firmware", then "FiiO firmware installed".
5. Take the microSD card out first, then unplug: the player restarts by itself into FiiO's firmware, which shows "Updating" once more for a minute or two while it installs the same file, and deletes it.

## If something goes wrong

- **A track, preset or other file doesn't work:** `/.rippl/errors.log` on the card lists each file the player couldn't open or use, one line each: the time (from the clock once it's set, otherwise time since the player started), what went wrong, such as "can't play this format", "path too long" or "can't use the preset (line 4)", and the file's path. Files that are allowed to be missing, like lyrics, folder images and `clock.bin`, aren't listed. The same error twice in a row is written once, and one session (from a start or a USB session to the next) writes at most 64 lines, then a note that the rest isn't logged. The player makes the log at the first error and adds each line to its end; it's never cleared, so delete it to empty it. If the card can't be written, the player stops logging until the next start.
- **"isn't a valid image for this player" when going back to FiiO:** check that you copied the `HIFIECnn.IMG` file, not `MINIVnnn.IMG`, and that it's unchanged (its hash matches the one in FiiO's zip). Rippl 0.2.0 and earlier also refuse FiiO's 4.0.0 firmware: update Rippl first, or install FiiO 3.9.0 (`HIFIEC39.IMG`) and then FiiO's own update to 4.0.0. To switch to another custom firmware, go back to FiiO's first: Rippl only installs its own images and FiiO's.
- **The install doesn't take** (FiiO's firmware comes back after a short "Updating"): copy the file again, check its hash on the player's drive, and make sure it's the only `HIFIECnn.IMG` there. If it keeps failing, the only other way in is maskrom (below).
- **The player doesn't start at all:** every case so far was recovered through the chip's maskrom mode, which writes FiiO's firmware back from a computer over USB. It needs a patched `rkdeveloptool` and some care; the steps are in `docs/device-recovery.md`.
- **A "STALL" or "FAULT" screen** (white text on black with PC and LR lines): photograph it, then press the small RST hole rather than holding Power. RST keeps the player's memory, so Home → Diagnostics → Last start and Last stack still show what stopped; holding Power on battery wipes it.
- **The player doesn't turn on after an update:** press RST once and start it; then photograph Home → Diagnostics → Last update (what the installer did and the state it left the internal storage in), Last start ("#n after power-off" means the player never lost power when you switched it off) and Boot hold if present.
- **Reporting a problem:** include what you did, your FiiO version before installing, and the Firmware, Last start, Last step, Last stack, Last USB, USB errors and Last update lines from Home → Diagnostics.
