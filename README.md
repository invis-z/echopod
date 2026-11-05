# EchoPod library and cover size limits

A draft for discussion: why the player stops at a certain library size, and a proposed layout that removes those limits. Nothing here is implemented.

- **Measurements:** `docs/library-limits.md` on branch `claude/library-cover-limits` (commit `3b97475`).
- **Measuring tool:** `library_size_probe` on the same branch, which runs the player's real library builder on synthetic libraries with the player's memory and file sizes.
- **Branch:** this one has no history shared with `main`; it only holds this note.

## 1. Where it stops today

### Library (`/.echo/library.db`)

Builds were measured on a computer with the player's limits: 383,880 bytes of builder RAM, a 4 MB `library.db` and a 24 MB `build.tmp`. Tracks per album: 12.

| Names | Most tracks | Bytes per track | Stopped by |
|---|---|---|---|
| short (10 characters each) | 26,215 | 89 | RAM |
| typical (16/22/24) | 26,215 | 137 | RAM |
| long (30/70/70, e.g. classical) | 13,980 | 300 | 4 MB file |
| Chinese, Japanese, Korean | 26,215 | 132 | RAM |

With 3 tracks per album, typical names fill the 4 MB file at 25,509 tracks.

The limits, in the order a growing library hits them:

1. **RAM, 26,215 tracks, whatever the names.** To match playlist entries to tracks, the builder makes two hash tables sized to the power of two above 1.25 × the track count. One track past 26,215 doubles them to 65,536 slots (256 KB), which doesn't fit. They're built on every rescan, even without playlists (`builder_index_paths`, `library_builder_paths.c`).
2. **The 4 MB `library.db`.** About 30,500 typical tracks. Most of each track's bytes are its full path (`Artist/Album/01 - Title.flac`), stored again for every track although the folder records already hold the folder names.
3. **RAM, 37,303 tracks.** The final write holds four 2-byte values per track in RAM at once (`load_columns`, `library_builder_encode.c`).
4. **The 24 MB `build.tmp`.** About 30,000 tracks with long names, about 60,000 with typical ones.
5. **The format.** All indices are 16-bit: at most 65,535 tracks, albums, artists, genres and folders.

### Covers (`/.echo/covers.bin`)

- **File size:** 80 MB, so 2,673 albums at 31,360 bytes each. Cards first set up by older builds have a 32 MB file: 1,069 albums. Past that, albums show without art.
- **RAM:** about 10 bytes per album for the index during the cover pass, taken from the 384 KB the image decoder also needs. JPEG fits to roughly 17,700 albums. The very largest PNGs may stop fitting at roughly 2,000–2,700 albums (to be measured on a player).
- **Format:** 16-bit counts, 65,535 albums.
- **Time:** about 0.5 s per album to decode on the player, unless echo-sync built the covers on a computer.

### Neither file ever grows

The player reserves both files once, at a fixed size, and leaves existing files alone (`app/card_files`; the card code can't grow a file). Raising the sizes in code only helps new cards. Small libraries still use the full 4 MB + 80 MB.

### How big libraries get

A 1 TB card holds about 33,000 FLAC tracks or 125,000 MP3s, so 26,215 is within reach.

## 2. Quick fixes (current format)

1. **Skip the playlist table when there are no playlists**, or size it more tightly. Removes limit 1.
2. **A bigger `library.db`** (8 MB), deleted and reserved again when a rescan finds it too small. Nothing is lost: the file is rebuilt on every rescan anyway.

Together: about 37,000 typical tracks, with no change to the format, the PC tools or echo-sync.

## 3. Proposal: albums on top, tracks and covers in shards

### Decisions so far

- **No M3U playlists** and **no All Songs list** (Music → Songs). These were the only features that needed an index of every track; dropping them is what makes the tiers below work.
- Tracks are kept apart from the album index, in **shard files**, not one file per album: on FAT, thousands of small files waste a cluster each (2,600 albums × 32 KB ≈ 85 MB) and slow every directory operation.
- **Covers get their own shards**, separate from tracks, so a tag change or library rebuild never rewrites covers.
- **Covers keep their pixels as they are.** The 112 × 112 cover is drawn pixel for pixel (Now Playing, home art, Cover Flow centre). The 56 × 56 one is drawn 46 px wide and 78–104 px tall (Cover Flow sides). Storing less would blur them.

### Files

```
albums.db              small: about 50–100 bytes per album
  header
  albums               sorted; each: title, album artist, genre, year,
                       track count, running track total,
                       track shard number, offset and length of its block
  artists, album artists, genres      → lists of albums
  folders              tree; each folder → the albums it holds
  strings              album-level names only

tracks-00.bin, …       fixed size (1–4 MB each)
  album blocks back to back; each block holds the album's
  track records (title, file name, artist, folder, disc, number,
  duration, file size, file time) and its own strings

covers.idx             sorted by album key, 12 bytes per album:
                       album key, cover shard, record number, art stamp
covers-00.bin, …       fixed size (64–128 covers, 2–4 MB each),
                       records of 31,360 bytes as today
```

### What it fixes

- **RAM while building no longer grows with the library.** Each album is built on its own; only the album list is sorted globally, on the card, with the existing external sort.
- **The limit counts albums, not tracks.** A track is (album, position), so the 16-bit cap means 65,535 albums, about 780,000 tracks at 12 per album. Card space runs out first.
- **Files never need resizing.** A bigger library adds a shard. `albums.db` stays small enough for one generous fixed size.
- **Smaller.** Tracks store only their file name; the album's folder supplies the rest. A typical track drops from about 137 bytes to about 90.
- **Small libraries stop paying for big ones.** No 80 MB cover reservation: shards are created as albums arrive.
- **Faster rescans.** The size and time kept in each track record work as a tag cache: a file whose size and time match keeps its record without being read again. Today every rescan reads every file's tags (by the documented timing, about 13 ms per track: roughly 6 minutes at 26,000 tracks). Only shards holding changed albums are rewritten, each whole: copy the unchanged blocks, write the new ones.
- **No cover RAM limit.** The cover pass walks `covers.idx` and the album list side by side, both sorted by album key, instead of holding the index in RAM; the PNG decoder keeps its full memory.

### How each part reads it

| Part | Reads |
|---|---|
| Albums, Cover Flow | `albums.db` |
| Artists, Album Artists, Genres | group → albums in `albums.db` |
| An album's tracks | its block: one read in one track shard |
| Shuffle all | a random number up to the total, its album from the running totals, then that album's block |
| Folders, Audiobooks, Podcasts | folder → its albums, then the tracks in those blocks with that folder's number |
| A cover | binary search in `covers.idx`, then one read; the current shard stays open for Cover Flow |
| Resume, Up Next, Favorites | album key + file name, which stay valid across rescans (today's track numbers don't) |

### Covers in shards

- **Changes:** an album whose art stamp changes is decoded again into its own record.
- **New albums** fill free records first, then a new shard.
- **Removed albums** free their record. If holes pile up, records from the last shard move into them: a 31 KB copy each, no decoding.
- **echo-sync** writes exactly the shards the albums with art need, plus `covers.idx`, with the stamps the player computes. A synced library needs no decoding on the player.
- **Moving over:** today's `covers.bin` already has the shard layout (header, sorted index, stamps, records), so it can be read as shard 0 holding its 2,673 covers. The first rescan after updating keeps every cover.

## 4. Open questions

1. **Shard sizes.** Smaller means less waste and cheaper rewrites but more files. Proposed: tracks 1–4 MB, covers 64–128 covers (2–4 MB).
2. **The old 80 MB `covers.bin`.** Keep it as shard 0, or split it once to free its unused space?
3. **Albums vs. folders.** Albums come from tags and can span folders (CD1/CD2), and one folder can hold several albums. Reuse is per file (size and time), so a retagged file is read again and can move album. Is per-file reuse enough, or should a folder's listing also be compared?
4. **Very large albums.** Thousands of untagged tracks in one album, or a podcast with 2,000 episodes, must still be built without holding the album in RAM.
5. **Smaller covers.** A 256-colour palette per cover, made on the computer, halves covers to about 16 KB per album with some banding in gradients. Wanted?
6. **Saved references.** Check how resume, Up Next and favorites identify tracks today before moving them to album key + file name.
7. **Order of work.** Do the quick fixes (section 2) first, while the new format is built?

## 5. What changes

- **On the player:**
  - the library builder: per album, sharded output, tag reuse
  - the library reader and every browse screen
  - the cover pass and the cover reader
  - resume, Up Next and favorites
  - card files: shards created on demand
- **On the computer:** the PC indexer (`tools/library_index.py`), the cover exporter and echo-sync, which write the same files.
- **For users:** M3U playlists and Music → Songs go away. The first start after updating rebuilds the library; covers are kept.
