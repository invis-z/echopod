# Rippl library design

A new library and cover store for Rippl: one that doesn't run out of memory, grows with the card, rescans only what changed and keeps playback state across rescans. Nothing here is implemented yet.

- **Measurements of today's limits:** `docs/library-limits.md` on branch `claude/library-cover-limits`.
- **Measuring tool:** `library_size_probe` on the same branch. It runs the player's real library builder on synthetic libraries, with the player's memory and file sizes.
- **This branch** has no history shared with `main`; it only holds this note.

## 1. Where today's library stops

Measured with the player's limits: 383,880 bytes of builder RAM, a 4 MB `library.db` and a 24 MB `build.tmp`.

| Limit | Where | Reached at |
|---|---|---|
| Playlist path tables, sized to the track count | builder RAM | 26,215 tracks, any names |
| Fixed 4 MB `library.db` (full paths stored per track) | card | ~30,500 typical tracks, ~14,000 with long names |
| Four 2-byte values per track held during the final write | builder RAM | 37,303 tracks |
| Fixed 24 MB `build.tmp` | card | ~60,000 typical tracks |
| 16-bit indices | format | 65,535 tracks, albums, artists, genres, folders |
| Fixed 80 MB `covers.bin` | card | 2,673 albums (1,069 on cards set up with 32 MB) |
| Cover index held in RAM during the cover pass | builder RAM | roughly 2,000–17,700 albums, by image type |

Other problems:

- **Every rescan reads every file's tags:** about 13 ms per track, so about 6 minutes at 26,000 tracks.
- **Files never grow:** both files are reserved once at a fixed size, so small libraries pay for big ones.
- **Rescans lose playback state:** resume and Up Next store track positions, so any rescan that changes the library discards them.

A 1 TB card holds about 33,000 FLAC tracks or 125,000 MP3s.

## 2. Goals

1. **No size limit except the card.** Builder RAM must not grow with the number of tracks, albums or covers, and every count is 32-bit.
2. **Card use follows the library:** a small library uses a few MB; a big one adds space as it grows.
3. **Rescans read tags only for new or changed files.**
4. **A track keeps its identity across rescans** (resume, Up Next, playlists).
5. **Power loss at any moment leaves a working library:** either the old one or the new one.
6. **Every feature stays:** All Songs, Play all, Shuffle all, playlists, Folders, Audiobooks, Podcasts, Cover Flow, favorites, bookmarks.
7. **Upstream's UI keeps merging:** screens and the player read the library through `library.h`, as today.

**Design target:** 250,000 tracks and 20,000 albums, about twice what a 1 TB card of MP3s holds. The host tests and the size probe prove the design at this size: builder RAM, rescan and lookup speed, manifest and segment counts. It isn't a cap; the only limit is the card.

## 3. Track identity: 64-bit path hash

A track's ID is the 64-bit FNV-1a hash of its normalized path:

- **Normalized path:** relative to the card root, with `/` separators and ASCII letters lowercased. FAT ignores case, and playlists often spell paths differently.
- **No stored allocation:** the ID is computed from the path, so the player, Tide and playlist entries all reach the same value without a shared table.
- **Collisions:** about 1 in 3.7 billion at 100,000 tracks, and 1 in 590 million at 250,000. The builder detects duplicates (they sit next to each other after sorting by ID), keeps the first and skips the second.
- **Renamed or moved files** get a new ID. Their tags are read again, and saved references to them stop matching. Any scheme keyed by path has the same behaviour.

Albums get the same treatment: a **64-bit album key**, the FNV-1a hash of album artist and title.

- **Why:** today's 32-bit `library_album_key` collides too often at this scale: about a 1.2% chance at 10,000 albums and 4.5% at 20,000. Colliding albums would share a cover and a favorite.
- **No conversion:** nothing from today's files is carried over (section 8).

## 4. Storage: segments and a manifest

All of Rippl's files live in `/.rippl`; `/.echo` is no longer used. The card code creates any missing folders on the way to a file, so the subfolders need no new code.

The card code can reserve a fixed-size file and write inside it. It can't grow, delete or rename one. So everything lives in **segment files**: fixed-size files, reserved when first needed and reused after that, never deleted.

```
/.rippl/manifest.bin                two alternating copies, sequence number + checksum
/.rippl/library/L0000.BIN, …        library segments, 1 MiB each
/.rippl/covers/C0000.BIN, …         cover segments, 4 MiB each (133 covers)
/.rippl/settings.bin, resume.bin, upnext.bin, favorites.bin,
        bookmarks.bin, equalizer.bin, bluetooth.bin, …   the player's other files
```

- **Manifest:** the only file the reader trusts first. It holds:
  - the library generation number
  - for each stream: its segments in order and its committed length
  - a free list of reserved segments that aren't in use
  - where each table lives inside the index stream
- **Streams:** a list of segments read as one long byte range. There are three:
  - the **record stream**: track records, appended to
  - the **index stream**: all tables, rewritten on each rescan
  - the **cover stream**
- **Commit:** a rescan writes only to free segments or past a stream's committed end. It then writes the manifest copy that isn't current, with the next sequence number. Until that write completes, the reader uses the old copy and sees the old library unchanged.
- **Freed segments** go back to the free list in the same commit. The next rescan reuses them before reserving new ones.
- **Card space:**
  - 1,000 tracks: two or three library segments.
  - 100,000 tracks: about 15 MB of records and 6 MB of tables, so about 25 one-MiB segments.
  - If reserving a segment fails, the rescan stops and reports the card full. The old library stays.

## 5. What's stored

### Record stream (kept between rescans)

One variable-length record per track:

- path ID (8 bytes), file size, modified time
- folder ID: the hash of the folder path
- file name
- title, artist, album, album artist, genre, year, disc, number, duration, and the tag fields the builder already keeps

Records are written once and kept as long as the file is unchanged, so the stream **is** the tag cache. Records of removed or changed files become garbage. When garbage passes 25% of the stream, the next rescan copies the live records into fresh segments and frees the old ones (compaction).

### Index stream (rebuilt on each rescan)

All tables use fixed-size rows, so finding row *n* takes one read. Counts and numbers are 32-bit; IDs and album keys are 64-bit.

| Table | Rows | Used for |
|---|---|---|
| `ids` | (path ID, size, modified time, track number), sorted by path ID | rescan comparison; finding a saved ID; playlists |
| `tracks` | per track number: album, artist, genre, folder, duration, disc, number, record position | `library_read_track` |
| `albums` | sorted albums: title, album artist, genre, year, first track number, track count, album key | Albums, Cover Flow, an album's tracks |
| `album_keys` | (album key, album number), sorted by key | cover pass, favorites |
| `artists`, `album_artists`, `genres` | sorted names → lists of albums or tracks | browse screens |
| `folders` | tree rows → subfolders and tracks sorted by file name | Folders, Audiobooks, Podcasts |
| `title_order` | track numbers sorted by title | All Songs, Play all, Shuffle all |
| `playlists` | names → lists of track numbers | Playlists |
| `names` | strings for albums, artists, genres, folders | group and album names |

**Track numbers** are positions in `tracks`, assigned in album order and then disc and number. An album's tracks are a contiguous range, just like today. Track numbers are valid only within one generation; anything saved across rescans stores the path ID.

**Lookups by ID** (resume, Up Next, playlist entries) are a binary search in `ids` through the block cache: about 17 sector reads at 100,000 tracks, with no RAM table.

## 6. Rescan

The builder keeps today's parts: the scan walker, tag readers, sort keys and collation, and the external sort with its fixed buffer and 32-way merge. Every step streams; RAM stays at today's fixed arena.

1. **Walk the card.** For each audio file, write (path ID, size, modified time, path) to a temporary stream, and sort it by path ID.
2. **Compare with the old `ids`**, both sorted, side by side:
   - **unchanged** (same size and time): keep the record position; no file is opened
   - **new or changed:** read the tags and append a new record
   - **gone:** count its record as garbage
   - **compaction**, if due: copy the kept records as they pass
3. **Order the tracks.** Sort the live (album sort key, disc, number, record position) entries and assign track numbers. Then write `tracks`, `albums`, groups and `folders` with further sorts, the way today's builder does.
4. **Write the rest:** `title_order`, `ids` with the new track numbers, and `album_keys`.
5. **Playlists:** hash each entry's path, look it up in the new `ids`, and write the list. No path tables.
6. **Commit** the manifest. Temporary streams use free segments and are freed in the same commit.

The first build after updating reads every file, as today. Later rescans read only changed files; the target is seconds for a typical change at 100,000 tracks, to be measured.

## 7. Covers

- **`covers_index`:** a table in the cover stream, with rows (64-bit album key, segment, slot, art stamp) sorted by album key, 16 bytes each.
- **Records:** 31,360 bytes as today (112 × 112 and 56 × 56), 133 per 4 MiB segment.
- **Cover pass:** walks `album_keys` and `covers_index` side by side, both sorted by key, so the index isn't held in RAM and the image decoder keeps all its memory.
  - Same stamp: keep the cover.
  - Changed or new: decode into a free slot, or a new segment when none is free.
  - Album gone: free its slot.
  - The new index is committed through the manifest.
- **Fragmentation:** if free slots pile up, covers from the last segment move into them, a 31 KB copy each with no decoding.

## 8. Saved state

| Data | Today | New |
|---|---|---|
| Resume | track position + whole-queue plan, dropped when the library fingerprint changes | path ID + position. The queue plan is kept while the generation is unchanged; after a rescan, playback resumes the same track in its album |
| Up Next | 100 × 16-bit positions, dropped on any change | 100 × path ID (800 bytes), kept across rescans; entries no longer found are skipped |
| Favorites | 32-bit album key | 64-bit album key |
| Bookmarks | 32-bit path keys | unchanged: few entries, all spoken-word files |
| Library fingerprint | hash of section offsets | the generation number |

**No conversion on update.** Nothing in `/.echo` is read. The first start builds a new library and new covers from the card, and everything else starts fresh: settings, EQ, Bluetooth pairings, bookmarks, resume, Up Next and favorites. `/.echo` stays on the card; removing it is up to the user.

## 9. Code changes and merging

- **`library.h`** keeps its shape:
  - 16-bit fields become 32-bit (`LIBRARY_NONE` becomes `0xFFFFFFFF`)
  - `library_read_index` returns 32-bit values
  - added: `library_track_id(library, track)` → 64-bit ID, and `library_find_track(library, id, &track)`
  - `library_open` takes the manifest instead of `library.db`
- **New or rewritten:**
  - the builder's ordering and encoding steps
  - the reader
  - segment and manifest code, replacing `card_files` for library and covers
  - the cover pass and cover reader
  - resume and Up Next encoding
- **Mechanical:** callers that hold track or album numbers as `uint16_t` widen to `uint32_t`: the player, play queue, Up Next, spoken-word code and some screens. Those files are upstream's, and these one-line changes are the main merge cost outside `src/library`.
- **Kept as is:** tag readers, sort keys, the scan walker, the external sort, cover decoding, the browse screens' logic.
- **Upstream changes to the library module** stop merging and are ported by hand.

## 10. Tide: the computer tools

Everything a user runs on a computer moves into one tool, **Tide**, rewritten for the new format. Tide builds the same files the player builds (segments, manifest, covers), so a synced card needs no rescan and no cover decoding on the player.

```
tide sync E:          build the library and covers on the computer and write them to the card
tide bookmarks E:     list audiobook bookmarks
```

| Today | In Tide |
|---|---|
| `echo_sync.py`, `echo_sync.bat` | `tide sync` |
| `echo_bookmarks.py` | `tide bookmarks` |
| `library_index.py` | `tide/library.py`, rewritten for segments and the manifest |
| `cover_export.py` | `tide/covers.py`, rewritten for cover segments |
| `tag_skim.py` | `tide/tag_skim.py`, part of the library builder |
| card detection and writing, inside `echo_sync.py` today | `tide/card.py` |

- **Layout:** a `tools/tide/` package, with `tools/tide.bat` (Windows, double-click) and a `tools/tide` launcher.
- **Names:** "EchoPod" in its messages becomes Tide and Rippl.
- **Staying in `tools/`:** the tools CMake and the tests run, which users never do:
  - firmware build: `make_image.py`, `font_build.py`, `glyph_pack.py`, `lang_pack.py`, `fold_table.py`
  - tests and checks: the `*_fixture.py` files, `check_*.py`, `target_audit.py`, `make_test_library.py`
  - `save_files.py` (development helper)
  - `patch_rkdeveloptool.py`, a one-off step in the recovery guide

## 11. Plan

| Step | Work | Done when |
|---|---|---|
| 1 | Segments and manifest on the host, with power-loss tests: cut the write at every step | every cut leaves the old or the new state |
| 2 | Builder and reader on the host behind `library.h` | random libraries read back identically to today's builder; the probe builds 250,000 tracks in the player's RAM |
| 3 | Rescan comparison and compaction | changing one album reads only its files |
| 4 | Player switch-over, 32-bit widening, path IDs in resume and Up Next | runs on the player; existing tests pass |
| 5 | Covers | 20,000 albums without RAM growth |
| 6 | Tide (section 10) | a card synced by Tide reads the same as one built on the player |

Nothing reaches the player before step 2's numbers hold.

## 12. Decisions

- **M3U playlists:** kept; each entry is one ID lookup.
- **Design target:** 250,000 tracks / 20,000 albums (section 2).
- **Segment sizes:** 1 MiB for the library, 4 MiB for covers, unless measurements say otherwise.
- **Old files:** `/.echo` (up to 108 MB) is left for the user to remove; the firmware doesn't delete it.
- **Folder:** everything moves to `/.rippl`, with `library/` and `covers/` subfolders.
- **Conversion:** none. The first start of Rippl resets everything (section 8).
- **Computer tools:** rewritten and renamed as Tide (section 10).

## Alternatives considered

- **Patching today's format** (skip playlist tables, stream the final write, recreate files larger, then widen indices): least code, but it leaves full rescans and lost resume. Most of the code changes anyway once indices widen.
- **Albums on top with per-album track shards** (the earlier draft): makes All Songs and playlists hard, and needs per-album shard rewrites. Here a flat track table plus sorted index tables does the same job, and All Songs is a 4-byte-per-track table.
