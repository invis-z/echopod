# Library database format (version 3)

The PC indexer (`tools/library_index.py`) writes the library database. The device reads it (`src/library`). Everything is sorted on the PC, so the device never sorts. It reads fixed-size records by index and strings by offset, a few hundred bytes at a time.

- Location on the card: `/.rippl/library.db`, written by the PC tools. The player builds and reads the Rippl format below instead.
- Byte order: little-endian
- Indices: `u16` (at most 65,535 tracks, albums, groups and folders)
- `0xFFFF` means "none"

## Header (128 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic `EMDB` |
| 4 | `u16` | version = 3 (the player rebuilds a library of any other version) |
| 6 | `u16` | section count = 10 |
| 8 | 10 × 12 B | section directory, in the order below |

Each section directory entry is `u32 offset`, `u32 count`, `u16 record size`, `u16 music count`. The music count is set for tracks, albums, artists, album artists, genres and title order (0 elsewhere); see Spoken word below.

| # | Section | Record size | Content |
|---|---|---|---|
| 0 | tracks | 24 | grouped by album in album order, then disc, track number and title |
| 1 | albums | 16 | sorted by title, then album artist |
| 2 | artists | 12 | track artists, sorted by name |
| 3 | album artists | 12 | sorted by name |
| 4 | genres | 12 | sorted by name |
| 5 | folders | 16 | depth-first; folder 0 is the music root (empty name) |
| 6 | title order | 2 | track indices sorted by title, then artist (All Songs) |
| 7 | playlists | 12 | M3U playlists, sorted by name |
| 8 | links | 2 | shared `u16` lists that groups, playlists and folders point into |
| 9 | strings | 1 | NUL-terminated UTF-8; offset 0 is the empty string; duplicates stored once |

## Records

| Record | Layout |
|---|---|
| **Track** | `u32 title`, `u32 path`, `u16 album`, `u16 artist`, `u16 genre`, `u16 folder`, `u32 duration_ms`, `u8 disc`, `u8 number`, `u16 chapters` |
| **Album** | `u32 title`, `u16 album_artist`, `u16 genre`, `u16 year`, `u16 first_track`, `u16 track_count`, `u16 cover` |
| **Group** (artist, album artist, genre, playlist) | `u32 name`, `u32 first_link`, `u16 link_count`, `u16 reserved` |
| **Folder** | `u32 name`, `u16 parent`, `u16 subfolder_count`, `u32 first_link`, `u16 track_count`, `u16 reserved` |

Field notes:
- `u32` string fields hold an offset into the strings section.
- Track `path` is relative to the music root and uses `/`. A track on the player's internal storage has a path under `:internal/`, a top-level folder no FAT or exFAT card can hold (`:` isn't allowed in names): `:internal/Artist/Album/01.flac` is `Artist/Album/01.flac` on internal storage. The folder is sorted among the root's names by its bytes like any other, and its folder record is named `:internal` (the player shows it as "Internal storage"). A library built without internal storage (Tide with no internal drive found, or a player whose internal volume didn't mount) has no such paths. Every string is Unicode NFC (composed). FAT cards store names in NFC, but macOS lists them decomposed (NFD), so the indexer recomposes paths, playlist names, playlist entries and tags. The device then matches paths against the card's names without any Unicode tables. Track `artist` indexes the artists section, and `genre` indexes genres.
- Track `chapters` is the number of chapters in the file's `moov.udta.chpl` atom (Nero chapters, written by m4b-tool, mp4v2 and ffmpeg): 0 when there is none, when it is unreadable or out of order, or when the atom uses a 64-bit size. It holds at most 255. A library built before this field existed has 0 there, and the format version stays 3.
- An album's tracks are `tracks[first_track .. first_track + track_count)`. Album `cover` is reserved and always `0xFFFF`. Covers live in their own store, keyed by album key (see Covers).
- A group's albums are `links[first_link .. first_link + link_count)`. Artists list every album containing at least one of their tracks. Artist and album-artist albums are ordered by year, then title; genre albums are ordered by title. Within each section the groups' links are consecutive and in group order (group `n + 1` starts where group `n` ends). Cover Flow relies on this for album artists to read its order straight from `links`; a file that breaks it gets Cover Flow in album order instead.
- A playlist's links are track indices in playlist order. Its name is the file name without `.m3u` or `.m3u8`.
- A folder's items are `links[first_link ..]`: first `subfolder_count` folder indices (sorted by name), then `track_count` track indices (sorted by disc, number, file name).

## Segment store

`library/segment_store` holds Rippl's library and covers. Data lives in streams: chains of fixed-size segment files, 1 MiB in `/.rippl/library/` (up to 512) and 4 MiB in `/.rippl/covers/` (up to 256), reserved when first needed and reused after that, never deleted. `/.rippl/manifest.bin` holds two 4 KB copies of the manifest: a sequence number, each stream's pool, first segment, segment count and length, 16 regions (stream, offset, length), 8 values, the next-segment table and an FNV-1a checksum. The newer valid copy wins when the store opens. A change is a transaction: `segment_store_begin`, then appends (to a stream, or to one `segment_store_start` emptied) and writes that never touch bytes the committed manifest uses, then `segment_store_commit` writes the other manifest copy. New data only goes to segments no committed stream uses or past a committed stream's end, so power lost at any point leaves the old state or the new one; segments a commit no longer uses are free for the next one. `test_segment_store` cuts the power at every write, with torn writes, and checks both.

## Spoken word

A track is spoken word when its first folder is `Audiobooks`, `Audiobook`, `Podcasts` or `Podcast` (ASCII case-insensitive; `library/spoken_path`, `spoken_of` in the indexer). Tracks on internal storage have `:internal` as their first folder, so they are always music. Spoken word is kept out of the Music lists and lives on the Audiobooks and Podcasts shelves, which browse its folders.

Every sort and grouping key of a spoken track starts with a spoken flag that sorts after music, so spoken word never shares an album, artist, album artist or genre with music: an author with music too appears twice, once among the music groups and once among the spoken ones. Each of those sections and the title order therefore hold their music entries first, then the spoken ones, and the header's music count says where the music ends. Tracks follow album order, so spoken tracks come last too. The Music lists (Cover Flow, Albums, Artists, Album Artists, Genres, Songs, shuffle) only read the first `music count` entries; folders and playlists show everything. Covers are built for every album, spoken ones included.

## Sorting

Sort keys are case-insensitive and accent-insensitive (Unicode NFKD with combining marks removed), and a leading "The " is ignored. So "The Beatles" sorts as "beatles" and "Björk" as "bjork".

## Files and tags

The indexer lists every format stock plays, `.flac`, `.ogg`, `.mp3`, `.m4a` and `.m4b` (AAC or ALAC), `.wav`, `.wma`, `.ape`, `.dsf` and `.dff`, and also `.opus` and `.qoa`, skipping hidden files and folders (a name starting with `.`). QOA carries no tags, so its fields come from its path and its length from its header. Each field takes the first non-empty value, trimmed:

| Field | Vorbis comment (FLAC, Ogg, Opus) | ID3 (MP3, WAV, DSF, DFF) | MP4 atom | APEv2 (any case) | ASF (WMA) |
|---|---|---|---|---|---|
| title | `title` | `TIT2` | `©nam` | `Title` | `Title` |
| artist | `artist` | `TPE1` | `©ART` | `Artist` | `Author` |
| album artist | `albumartist` | `TPE2` | `aART` | `Album Artist` | `WM/AlbumArtist` |
| album | `album` | `TALB` | `©alb` | `Album` | `WM/AlbumTitle` |
| genre | `genre` | `TCON` (ID3v1 numbers become names) | `©gen` | `Genre` | `WM/Genre` |
| date | `date` | `TDRC` (`TYER` in ID3v2.3) | `©day` | `Year` | `WM/Year` |
| disc | `discnumber` | `TPOS` | `disk` (first number) | `Disc` | `WM/PartOfSet` |
| track | `tracknumber` | `TRCK` | `trkn` (first number) | `Track` | `WM/TrackNumber` |
| album version | `albumversion` | `TXXX` described `ALBUMVERSION` (any case) | `----` named `ALBUMVERSION` (any case, any mean) | `AlbumVersion` | not read |

**Album versions.** When a track has both an album tag and an album version (the `ALBUMVERSION` tag Navidrome uses to tell editions apart; Picard users usually set it from `%_releasecomment%` with a script), the album title becomes `Album (Version)`, for example `Abbey Road (2019 Remix)`. The version is part of the title everywhere (lists, Now Playing, Cover Flow, sorting) and of the album key, so two editions of one album are two albums, each with its own cover and favorite. Albums without a version keep their key, so favorites and stored covers of those survive. A version without an album tag is ignored (the folder name stays the album). Navidrome also reads `MUSICBRAINZ_ALBUMCOMMENT` as the version; Rippl doesn't.

An MP3 without ID3v2 uses its ID3v1 tag. The duration is the container's length rounded to the nearest millisecond (FLAC and DSF sample counts, Ogg's last granule (for Opus minus the header's pre-skip, at 48 kHz), an MP3's Xing/Info frame count or its bitrate, MP4's track duration, WAV data length, APE frame counts, ASF play duration minus preroll, DFF DSD data length). `tools/tag_fixture.py` writes one small file per format to `tests/fixtures/tags`, and both the Python indexer and the device reader (`src/library/track_tags`) are tested against them.

On the device, `track_tags_read` reads through a `ByteSource` with small bounded reads, keeps the first 255 bytes of each value (UTF-8, trimmed as Python's `strip()` does; on both sides, invisible marks and control codes are dropped, bytes 0x80–0x9F read as Windows-1252, and the typographic spaces U+2000–U+200A, U+2028, U+2029, U+202F and U+205F become plain spaces, which the fonts can draw) and computes durations in double precision in the same order as mutagen, so the rounded milliseconds match. It also reports where the cover is: a front cover (picture type 3, or MP4's `covr`) wins over any other picture. For Ogg, the cover is the base64 `METADATA_BLOCK_PICTURE` text, and its offset counts bytes of the Ogg stream from the start of the comment packet's first page body, page headers excluded; the cover pass decodes that text as it reads it. Known differences from the PC indexer: values aren't recomposed to NFC, and an ID3v2.3 tag with tag-wide unsynchronisation is read without undoing it.

## Building on the device

`library/library_builder` builds a database that means the same as the PC indexer's, from tracks given in scan order (the PC indexer's sorted path order), with a block of RAM and scratch space on the card: on the player, a stream of the segment store (`library/segment_scratch`) that only takes the 1 MiB pages the builder touches. Inside it, every track, album, group, folder and rank number is 32-bit, and the scan's regions are a share of the scratch space (rows an eighth, folders and their hashes a 512th per folder). `library_builder_finish` writes this file and refuses a library with more than 65,535 of anything; `library_builder_finish_wide` writes the Rippl format below. 20,000 tracks are tested inside 360 KB of RAM against the reference builder, and 250,000 through the Rippl format. RAM only ever holds what one step needs: buffers and small caches. Nothing in RAM grows with the number of tracks, albums or folders: wherever a step needs a value by track, album or folder number, the values go to the card as (number, value) pairs and come back in number order through the external sort.
- During the scan each track becomes a 48-byte row on the card (text offsets and lengths, folder, year, disc, number, duration) and its texts go to a heap on the card (a text equal to the previous track's same field is stored once). Folders become 8-byte rows, each with a 64-bit FNV-1a hash of its full path. In sorted input a folder never comes back once left; when sibling folders arrive out of order, a pass after the scan sorts the path hashes, keeps the first-created folder of each path, renumbers the folders and rewrites the track rows in place.
- Before ranking, tracks marked as having no album-artist tag (a spare row byte) get their shared album artist: one record per such track (folder, track, artist text, album and artist sort keys) is sorted by folder, album key, artist key and track; one pass picks each group's winner, a second writes (track, artist text) pairs, which are sorted by track and merged back into the rows in one sequential rewrite. Nothing runs when no track is marked.
- Each field is then ranked: one record per run of equal text (its sort key, its bytes and its first track) goes through a stable external merge sort on the card (sorted chunks in RAM, then up to 32-way merges), giving a dense key rank per track, a spelling rank for genres, and each group's first spelling. Folder names are ranked by key, then bytes.
- Every view is built from integer tuples with the same sort, in the PC's order: albums (grouped by album artist and album; first sight, earliest non-zero year, most common genre spelling with ties to the first seen; members by disc, number and title), album order (title, then album artist, then first sight), the final track order, artist and album-artist links by year and title, genre links by title, preorder folders (sorted rank paths) with their links, and All Songs by title then artist. Playlists resolve through two tables sorted on the card, (casefolded path hash, position) and (casefolded file-name hash, position), searched through a small cache and confirmed against the paths rebuilt from the card.
- `library_builder_finish` writes the file in two passes over the card data: records with string offsets counted in emission order, then the strings, through a 512-byte buffer; the `EMDB` magic goes in last so an interrupted build reads as no library. Titles and paths are written per track and names per group, without the PC's global de-duplication, so the bytes differ from the PC's file while every record, link and string means the same. It reports running out of memory or scratch room rather than truncating.
- Tests decode both files and compare them field by field (`tests/library/library_compare.c`): the fixtures `small_library_tracks.db` and `builder_random/`, and seeded random libraries of 1 to 20,000 tracks (`tests/library/random_library.c`) against the previous in-RAM builder kept as a test-only reference (`tests/library/reference/`).

`library/library_scan` walks the card for it through a `ScanFileSystem` (list a folder, open a file): each folder is read whole, names starting with `.` are dropped, the rest sorted by their UTF-8 bytes (the PC's part-wise path order), and the walk goes depth first without recursion, with one folder open at a time. Files with a stock extension get `track_tags_read` and `library/track_fields` (the fallbacks below), then go to the builder. A file whose tags can't be read is skipped and counted. Each folder listing keeps every entry's size and modification stamp next to its name.

**Scan cache.** A job may carry a `ScanCache`: the previous scan's records (a byte source) and a sink for the new ones. A record is `u16 length`, `u8 kind` (1 track, 2 unreadable), `u8 flags` (bit 0: album artist guessed), `u32 size`, `u32 stamp`, `u32 duration_ms`, `u16 year`, `u8 disc`, `u8 number`, `u8 chapters`, then NUL-terminated texts: the path, and for a track its title, artist, album artist, album and genre, at most 4,096 bytes in all. Records are written in walk order, which is the part-wise path order, so the scan merges the previous records with the walk like a sorted merge: records whose path sorts before the current file are dropped (the file is gone), a record with the same path, size and stamp is used instead of opening the file (counted in `ScanStats.reused`), and anything else is read as before. Every file found writes its record to the new set, so the next scan starts from this one. Two 8 KB buffers come out of the job's memory only when a cache is given. A record that doesn't parse ends the use of the previous records for that scan. Its test scans the fixtures twice (the second scan opens no audio file and writes the same records), and with one record's size changed, one record removed and one stale record added. An eighth of the scratch block (16–96 KB) holds the folder listings unless a separate listing buffer is given; the builder gets the rest and the scan job's scratch file. Its test scans `tests/fixtures/tags` through the simulator's file HAL and must mean the same as `tests/fixtures/tags_library.db`, which `tags_library_up_to_date` re-indexes with the PC tool. On 2026-10-18 it also matched the PC indexer byte for byte on a 50-track sample folder.

## Rippl library format (version 1)

Rippl's own library uses the same sections, order, sorting and meaning as version 3, with 32-bit numbers. It lives in a stream of the segment store (`library/segment_store`, see Segment store above), not in `library.db`: `library/segment_stream` gives the builder a sink that appends to a working stream and gives the reader a source over the committed one.

- Header: 256 bytes, magic `RLIB`, `u16` version 1, `u16` section count 13, then thirteen 16-byte entries: `u32 offset`, `u32 count`, `u16 record size`, `u16` 0, `u32 music count`. After the strings come the path IDs, the album keys and the album ranks.
- A track's path ID is the 64-bit FNV-1a hash of its library path with ASCII letters lowercased (`library_path_id`), so the same file keeps its ID across rescans, whatever its track number. `library_track_id` gives a track's ID and `library_find_track` finds the first track with an ID by a binary search in the path IDs: about 18 reads at 250,000 tracks.
- No `none` value is stored except a folder's parent: the root's is `0xFFFFFFFF`. Albums have no cover field; the reader reports `LIBRARY_NONE`.

| Record | Size | Layout |
|---|---|---|
| Track | 32 | `u32 title`, `u32 path`, `u32 album`, `u32 artist`, `u32 genre`, `u32 folder`, `u32 duration_ms`, `u8 disc`, `u8 number`, `u16` 0 |
| Album | 24 | `u32 title`, `u32 album_artist`, `u32 genre`, `u32 first_track`, `u32 track_count`, `u16 year`, `u16` 0 |
| Group | 12 | `u32 name`, `u32 first_link`, `u32 link_count` |
| Folder | 20 | `u32 name`, `u32 parent`, `u32 subfolder_count`, `u32 first_link`, `u32 track_count` |
| Title order, links | 4 | `u32` |
| Path ID | 12 | `u64 id`, `u32 track`, one per track, sorted by ID, then track |
| Album key | 12 | `u64 key`, `u32 album`, one per album, sorted by key, then album |
| Album rank | 4 | `u32`: the album's position among the album keys, one per album |

**On the player.** `library/library_rebuild` runs a rescan as one segment store transaction, inside the rescan overlay: stream 1 holds the library, streams 2 and 3 take turns holding the scan cache's records (value 0 names the current one), stream 7 is the builder's scratch, and region 0 is the library (stream 1, offset 0, its length). The cover pass runs in the same transaction (see Covers). The scan reads the previous records from the committed stream and writes new ones to the other, the builder writes the library to a fresh stream 1, and the commit drops the old records and the scratch, so their segments are free for the next rescan. If it fails, or the card has no room for a segment ("No room for the library on the card"), the transaction is cancelled and the old library stays. `app/library_store` opens the manifest and reads region 0 through a block cache; the store's sequence number is mixed into `library_fingerprint`, so resume notices any rescan.

**Saved playback.** `/.rippl/resume.bin` (magic `EMRS`, version 1, 60 bytes) keeps the playing track's path ID, its position and the queue plan, with the library fingerprint. When the fingerprint matches, the queue is rebuilt as it was; after a rescan the player finds the track by its ID and queues its album with the same shuffle mode. `/.rippl/upnext.bin` (magic `EMUN`, version 1, 812 bytes) holds a `u16` count and up to 100 path IDs (`u64`) with an FNV-1a checksum; songs no longer in the library are skipped when it loads. A library without path IDs (version 3, from the PC tools) is searched track by track. `test_library_scan` rebuilds the fixtures into a fake card twice (the second rebuild opens no file), with another folder, and with no room left.

`library_open` reads either format from the magic (`Library.wide`), and `library.h` gives 32-bit numbers for both (`LIBRARY_NONE` is `0xFFFFFFFF`; a version-3 `0xFFFF` parent or cover reads as it). Tests build every random library and the PC fixture in both formats and compare them field by field, and build 250,000 tracks with the player's 360 KB through segment scratch into a stream, commit it, read it back and find sampled tracks by path ID (`test_large_library`): about 39 MB. `test_library_builder` finds every track of a random library by its ID, spelled in either case.

## Fallbacks for missing tags

| Missing | Used instead |
|---|---|
| title | file name without its number prefix |
| artist | album artist, then "Unknown Artist" |
| album artist | artist, then shared per album (below) |
| album | folder name (not for the card root or the `:internal` folder: "Unknown Album") |
| genre | "Unknown Genre" |
| disc and number | `<disc>-<track> - ` or `<track> - ` file-name prefixes, otherwise disc 1, track 0 |
| year | first four digits of `date`, otherwise 0 |

**Albums without an album-artist tag.** Tracks whose album artist came from the artist (no album-artist tag) and that share a folder and an album title (by sort key) are one album: they all get the album artist that most of them have as artist, ties going to the one whose sort key comes first, spelled as the earliest such track spells it. So an album with a featured artist on some tracks ("The Weeknd & Justice" among "The Weeknd") stays one album instead of splitting. Tracks with an album-artist tag keep it, and the same album split over folders (`CD1`, `CD2`) still joins when their artists agree. The PC indexer does this in `guess_album_artists` before building; the device builder does it as a pass over the track rows before ranking (below).

## Playlists

The indexer reads every `.m3u` and `.m3u8` file under the music root, skipping hidden folders. A file is decoded as UTF-8 (with or without a BOM), or as Windows-1252 if that fails. Blank lines and `#` lines are ignored, and so are URLs.

Each entry is matched to a track, ignoring case and treating `\` as `/`:

1. relative to the playlist's folder
2. relative to the music root
3. otherwise by the longest matching run of trailing path parts, so absolute Windows paths such as MusicBee's `D:\Music\Artist\Album\01 - Song.flac` still match. A tie counts as not found.

Entries that match nothing are left out, and the indexer reports how many each playlist lost.

On the device, the scanner notes each `.m3u`/`.m3u8` path during the walk (the PC's order) and reads them after the tracks, once `library_builder_prepare` has fixed the track order and built the path indexes: an exact table keyed by the casefolded path (Python's `str.casefold`, from a second generated table; the last of equal keys wins, as in a Python dict) and one keyed by casefolded file name for the suffix match. `library/playlist_reader` checks the whole file for strict UTF-8 (a BOM is skipped) or decodes Windows-1252 with U+FFFD for its five undefined bytes, splits lines like `str.splitlines` (U+0085, U+2028 and U+2029 only in UTF-8) and trims them; each entry is resolved at once, so entries are never stored. Candidates go through `posixpath.normpath` rules, including its two leading slashes. Playlists are sorted by sort key and then name, and their names share the string table with everything else. `scan_music/` (nested folders, a hidden folder, an AppleDouble file and three playlists in UTF-8 with a BOM, Windows-1252 and plain UTF-8) and `builder_random/with_playlists` (30 random playlists, 379 entries) check it byte for byte.

## Album keys and favorites

Favorites outlive a re-index, so they don't store album indices. An album's key is the 64-bit FNV-1a hash of its album artist, a `0x1F` separator and its title (`library_album_key`); a 32-bit key would collide about 4.5% of the time among 20,000 albums, and colliding albums would share a cover and a favorite. `/.rippl/favorites.bin` holds the magic `EMFV`, `u16` version 1, a `u16` count, the `u64` keys and an FNV-1a checksum. At most 256 albums can be favorites. An album that leaves the library just stops showing up. In the Rippl format each favorite is found by a binary search in the album keys; a version 3 library is searched album by album.

## Bookmarks file

`/.rippl/bookmarks.bin` (4 KB, reserved by the player) remembers where each audiobook and podcast episode stopped (`playback/bookmarks`). Like favorites, it is keyed by path hashes, so it outlives a re-index.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic `EMBK` |
| 4 | `u16` | version = 1 |
| 6 | `u16` | count (at most 128) |
| 8 | `u32` | reserved (0) |
| 12 | count × 16 B | bookmarks, most recent first |
| 12 + 16 × count | `u32` | FNV-1a checksum of everything before it |

Each bookmark is `u32 key`, `u32 track key`, `u32 position_ms`, `u16 chapter`, `u8 flags` (bit 0: finished), `u8 reserved`. The key is the FNV-1a hash of the book's folder path relative to the music root (`Audiobooks/The Long Road`), or of the track's own path for a single-file book or a podcast episode; the track key hashes the track's path. `chapter` is the track's place among its folder's tracks, checked against the track key before it's used. A zero-filled or damaged file reads as no bookmarks.

## Update record

`/.rippl/update.bin` (4 KB, reserved by the player) keeps what the firmware installer last did, so Diagnostics → Last update can show it after the player has been switched off (`app/update_record`, written by `board/update.c` at the end of a USB session that found a `HIFIECnn.IMG`). Every value is little-endian.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | magic `EUPD` |
| 4 | `u8` | result: 0 none, 1 installed, 2 failed, 3 refused, 4 already installed |
| 5 | `u8` | detail: the last progress mark shown (5 % steps) for installed and failed, the refusal code (`E` number) for refused |
| 6 | `u8` | 1 when the internal storage answered CMD13 after the install |
| 7 | `u8` | reserved (0) |
| 8 | `u32` | the eMMC status word that CMD13 returned (card state in bits 12:9, ready-for-data in bit 8) |
| 12 | `u32` | checksum: the bitwise complement of a 31-multiplier sum over the first 12 bytes |

A zero-filled or damaged file reads as no record.

## Music folders file

`/.rippl/folders.bin` (reserved at 4 KB by `card_files`; 140 bytes used, little-endian) lists the top-level folders Settings → Music folders leaves out of the library. A missing, zeroed or damaged file leaves nothing out.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `EMMF` |
| 4 | 1 | version, 1 |
| 5 | 1 | count, at most 32 |
| 6 | 2 | reserved, 0 |
| 8 | 128 | 32 folder keys (`u32`), the first `count` used, the rest 0 |
| 136 | 4 | FNV-1a of bytes 0–135 |

A folder key is the FNV-1a hash of the folder's library path: its name as the file system lists it (UTF-8, case kept) for a folder in the card's root, `:internal/` followed by the name for a folder in the internal storage's root. Keys name folders rather than positions, so new folders are included and a folder that is gone keeps its choice. Both the player's scan and fingerprint and `library_index.py --folders` / Tide skip a top-level folder (never a file, never a deeper folder) whose key is listed; `library_index.encode_excluded_folders` writes the file. `tests/fixtures/scan_music_folders.bin` leaves `Artist B` and `Lists` out of `scan_music`, and `scan_music_folders.db` is the PC indexer's library for it (`scan_music_folders_up_to_date`); `test_library_scan` scans the same folder with the same file and must mean the same.

## Clock record

`/.rippl/clock.bin` (32 bytes, little-endian) is written by Tide as the last step of a sync, so the player can set its clock chip from the computer's when the USB session ends. The player never writes it.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `ECLK` |
| 4 | 1 | version, 1 |
| 5 | 1 | flags: bit 0, a daylight saving rule follows; the other bits 0 |
| 6 | 2 | reserved, 0 |
| 8 | 4 | the computer's time as Unix seconds (UTC) |
| 12 | 2 | the computer's standard UTC offset in minutes, signed |
| 14 | 2 | the minutes daylight saving adds, 1–180 (0 without a rule) |
| 16 | 6 | when daylight saving starts: month (1–12), week (1–4, or 5 for the last), weekday (0 Sunday – 6 Saturday), a reserved 0, then the minutes after midnight of that day in standard time, signed (−10,080 to 10,080, so a change can be written as the 4th Thursday at 26:00) |
| 22 | 6 | when it ends, the same way, also in standard time |
| 28 | 4 | FNV-1a of bytes 0–27 |

Without a rule, bytes 14–27 are 0. Tide reads the computer's UTC offset with `time.localtime` over the next eight years (or two, if the rule changes within eight), finds each change to the second, and writes a rule only when one yearly rule in this form gives every change it found and the offset it sampled every six hours; otherwise it writes the current offset and no rule. The rule's dates and times are the computer's local ones, like a POSIX `TZ` rule's `Mm.w.d/time`, with the end moved from summer time into standard time.

The player accepts a record whose magic, version and checksum agree, whose time falls in 2024–2099, whose offset is −12:00 to +14:00, and whose rule, if any, has every field in range and starts and ends at different times; and only when its time differs from the record it saw when it started. It sets the chip to the time plus the standard offset (the chip keeps standard local time), stores the offset as its time zone setting (rounded to 15 minutes), sets Daylight saving to From computer (Off without a rule), and ignores the same record from then on. It reads the rule again from the file at every start.

## Covers

Covers live in the segment store with the library and are committed with it, in the rescan's one transaction. Each cover is the full cover (112 × 112 pixels) followed by the half cover (56 × 56), as little-endian RGB565 rows, top to bottom: 31,360 bytes. Slot `n` sits in a stream of 4 MiB segments at `(n / 133) × 4 MiB + (n % 133) × 31,360`, 133 covers to a segment.

- **Cover table:** stream 0, region 2. One 8-byte row per position in the library's sorted album keys: `u32 slot` (`0xFFFFFFFF` for no cover) and `u32 stamp`. Albums with the same key share a row's cover. To draw an album the player reads its rank (the album ranks section) and then that row: two 4-byte reads.
- **Cover records:** stream 4 or 5 (value 1 names it), with value 2 the number of slots used.
- **A rescan** walks the new library's album keys and the old library's (still committed in the old library stream) side by side, both sorted, so nothing about the covers is held in RAM. An album whose key and stamp match keeps its slot; new or changed art is decoded and appended after the last slot; covers of albums that are gone are left as garbage. When more than a quarter of the slots are garbage, the next rescan copies the live covers into the other stream, without decoding, and frees the old one. New covers are only ever appended past the committed end, so a cut leaves the old covers whole.
- **A full card:** when the card has no room for another segment, albums that need a new cover show without one and the rescan says so; the covers it had stay. If the library itself has no room, the rescan keeps the old library and its covers.

A stamp is FNV-1a over where the art came from (`/` plus the library path for embedded art, the folder plus the listed name for a cover file), then three little-endian `u32`s: for embedded art the track's size, the picture's length (for Ogg, the decoded image's length) and 1; for a cover file its size, its modification stamp (the music fingerprint's value, from the folder listing) and 2. A stamp of 0 means unknown: that album is decoded again. Finding the stamp opens each album's art every rescan, as before; only decoding is skipped.

`test_cover_build` rebuilds small cards into a fake card (reuse, new and replaced art, removed albums, compaction, a full card), `test_covers` reads covers across segments, and `cover_build_matches_reference` (`tools/check_cover_build.py`) builds covers for real art with `cover_build_tool`, compares them with Pillow's decode, and checks that a second build reuses every one. Tide writes the same store from a computer (`tools/tide`): `store.py` reads and writes the manifest and segments (it writes only to segments the committed manifest doesn't use, then the other manifest copy), `rippl_format.py` writes the Rippl format from the PC indexer's model, with path IDs, album keys and ranks, `records.py` writes the scan records in the player's walk order with the player's file stamps, and `covers.py` builds the cover table and records, keeping covers whose key and stamp match the card's and decoding the rest with Pillow. `cover_build_matches_reference` syncs the test music with Tide using the simulator's file stamps, checks with `cover_build_tool --compare` that its library means the same as the player's, and rebuilds the card with the player's code, which must keep every file's record and every cover.

The exporter takes, for each album:
1. the embedded picture of the first of its first four tracks (in track order) that has one: the track's first front cover (picture type 3), otherwise its first picture of any type. For Ogg Vorbis and Opus files these are the `METADATA_BLOCK_PICTURE` comments; a value that isn't valid base64 or a valid picture block is skipped
2. otherwise `cover`, `folder`, `front` or `albumart` (`.jpg`, `.jpeg` or `.png`, any case) in a folder holding its tracks

The image is cropped to a centered square and resized with Lanczos filtering, separately for each size. Albums without art are left out and listed when the export finishes; the device draws a plain placeholder for them.
