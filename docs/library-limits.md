# Library and cover size limits (investigation)

`docs/known-issues.md` says the player is tested up to 6,558 tracks, that larger libraries "may stop with an error when the library file (4 MB) is full", and that covers stop at 2,673 albums. This note measures where each limit actually is, what sets it, and what it would take to raise it. Nothing here changes the player; the only code added is the measuring tool.

## How it was measured

`tests/library/library_size_probe.c` (target `library_size_probe`, built with the host tests, not run by `ctest`) feeds synthetic libraries to the device builder (`library/library_builder`) with the device's limits:

- builder RAM: 383,880 bytes. Core 1's 384 KB minus the scan's own state (`ScanState`, 9,336 bytes on the device); the folder listings use the audio ring, so the builder gets the rest
- `library.db`: 4 MB; `build.tmp`: 24 MB (`app/card_files`)

It bisects the largest track count that builds and names the limit one more track hits. Four naming styles, path `artist/album/NN - title.flac`:

| Style | Artist, album, title (characters) | Example use |
|---|---|---|
| short | 10, 10, 10 | |
| typical | 16, 22, 24 | most pop or rock libraries |
| long | 30, 70, 70 | classical: work, performers and movement in the names |
| cjk | 6, 10, 12, 3 bytes each | Chinese, Japanese or Korean names |

```sh
build/host/tests/library_size_probe                 # device limits, 12 tracks per album
build/host/tests/library_size_probe 3               # 3 tracks per album
build/host/tests/library_size_probe 12 512 64 1024  # 512 KB RAM, 64 MB library, 1 GB scratch
```

The covers side was worked out from `library/cover_build` and `docs/architecture.md` (Covers on the device), not measured.

## Library: results

At the device's limits, 12 tracks per album:

| Style | Most tracks | `library.db` | Per track | Stopped by |
|---|---|---|---|---|
| short | 26,215 | 2.2 MB | 88.7 B | RAM |
| typical | 26,215 | 3.4 MB | 137.1 B | RAM |
| long | 13,980 | 4.0 MB | 300.0 B | library file full |
| cjk | 26,215 | 3.3 MB | 131.7 B | RAM |

With 3 tracks per album (singles, EPs) the per-album records weigh more: typical stops at 25,509 tracks (164.4 B/track) and long at 11,850 (353.9 B/track), both on the library file.

So today the ceiling is **26,215 tracks**, or about **12,000–14,000** with long names. The known-issues line is half right: for most libraries RAM runs out before the 4 MB file is full.

### What sets each limit, in the order they're hit

1. **RAM: the playlist path index, at 26,215 tracks, whatever the names.** `builder_index_paths` (`library_builder_paths.c`) builds two hash tables (exact path and file name) of `u16` slots, sized to the power of two above 1.25 × the track count, to resolve M3U entries. At 26,215 tracks they have 32,768 slots each; at 26,216 they double to 65,536 (256 KB for the pair, plus 4 bytes per track), which doesn't fit. It is built in `library_builder_prepare` on every rescan, **even when the card has no playlists**. Above 26,215 the error is "Library too big for the player's memory".
   - With a tighter table (5 % headroom instead of 25 %) the same index next doubles at 30,841 tracks.
   - Skipping it entirely moves the RAM limit to 37,303 tracks.
2. **The 4 MB `library.db`.** About 137 bytes per typical track, so about 30,500 tracks. Long names reach it first (13,980). The error is "No room for the library on the card".
3. **RAM: the final write, at 37,303 tracks** (found by skipping the path index). `load_columns` in `library_builder_encode.c` holds four `u16` columns per track (final order, album, artist, genre) in RAM at once, 8 bytes per track.
4. **The 24 MB `build.tmp`.** It grows by about 340 bytes per typical track (720 for long names) on top of a few MB of fixed use. It's only reached once the limits above are gone: long names at about 30,000 tracks, typical at about 60,000.
5. **The format: 65,535** tracks, albums, artists, genres and folders (`u16` indices, `docs/library-format.md`). Reaching it needs a format change.

### Where the bytes go

Per track, `library.db` holds a 24-byte record, 2 bytes in All Songs, 2 in its folder's links, and two strings: the title and the **full path** (`Artist/Album/01 - Title.flac`). The device builder stores both per track without de-duplicating (`docs/library-format.md`, Building on the device). The path dominates, and most of it repeats the folder names the folder records already hold.

## Covers: results

| Limit | Value | Set by |
|---|---|---|
| `covers.bin` size | 2,673 albums at 80 MB; 1,069 on cards set up by builds that made a 32 MB file | `(size − 16) / (8 + 4 + 31,360)` slots (`layout_for` in `cover_build.c`) |
| Format | 65,535 albums | `u16` cover count and `u16` record numbers (`COVER_BUILD_MAX_COVERS`) |
| FAT32 file | about 136,900 albums (4 GB) | the format cap is lower |
| RAM | about 10 bytes per album | the index in the cover pass's 384 KB |
| Card space | 31,372 bytes per album slot | 10,000 albums take 300 MB, 65,535 take 1.9 GB |
| Time | about 0.5 s per album decoded | `docs/known-issues.md`; 10,000 albums is about 1.4 hours on the first build |

The file is the only limit hit in practice. Past 2,673 albums the rest show without art.

**RAM beyond the file.** The cover pass keeps, per album, a key and stamp (8 bytes), a `u16` record number, and a claimed bit plus a used bit per slot: about 10.25 bytes. The decoder takes what's left: about 171 KB for JPEG, up to about 327 KB for the worst PNG (64 KB rows), next to about 35 KB for the builder's own state (`docs/architecture.md`). From these figures:

- **Worst-case PNG:** stops fitting at roughly 2,000–2,700 albums. The figures are rounded, so where in that range isn't clear, and it should be measured on the player before relying on it. Ordinary PNGs need far less.
- **JPEG:** fits up to roughly 17,700 albums.

## Neither file is ever resized

`card_files_prepare` reserves both files with `hal_file_reserve`, which leaves an existing file alone whatever its size (`fat_reserve` has no way to grow a file). Raising `LIBRARY_FILE_BYTES` or `COVERS_FILE_BYTES` would only help new cards.

- **`library.db`** is rewritten whole on every rescan, so deleting it and reserving it again at a new size loses nothing. Note that creating it sets `library_created`, which asks for a rescan.
- **`covers.bin`** is different: recreating it throws away every cover, and the next rescan decodes them all again (about 0.5 s per album).

## Options, cheapest first

1. **Don't build the playlist path index when there are no playlists, or size it tighter** (`library_builder_paths.c`, builder only, no format change).
   - Skipping it moves the ceiling from 26,215 to about 30,500 typical tracks, where the 4 MB file then stops it, or to 37,303 with a larger file.
   - Tighter sizing alone gives 30,841.
   - Skipping only helps cards without playlists. Building it on the first playlist instead would need the RAM the encode pass reuses later (`index_mark`).
2. **A larger `library.db`, recreated when it's too small.** 8 MB holds about 37,000 typical or 26,000 long-named tracks. Since the file is rebuilt every rescan, `card_files` (or the rescan, on "No room") could delete and re-reserve it.
3. **Stream the final write's columns** (`load_columns`) instead of holding four per-track columns in RAM. This removes limit 3. The next RAM limit wasn't measured.
4. **Store each track's file name instead of its full path** (format version 4: path = folder path + name). This cuts typical tracks from about 137 to about 90 bytes and long ones by more. It needs the PC indexer, echo-sync and the reader changed together.
5. **Covers: size `covers.bin` to the album count.** After a rescan, if albums exceed the slots, grow the file by recreating it larger: one full decode, and card space at 31 KB per album. The RAM index stays fine for JPEG to about 17,700 albums; the PNG margin should be measured first.

Options 1 and 2 together raise typical libraries from 26,215 to about 37,000 tracks without touching the format.
