# Release notes

Rippl is a fork of EchoPod (github.com/BasilSkyWalk/echopod), starting from EchoPod 0.12.0; EchoPod's own release notes are in its repository.

## Unreleased

- The library is built into segment files in `/.rippl/library`, which grow with the music instead of a fixed 4 MB `library.db` and 24 MB `build.tmp`, and a rescan reads only files that are new or changed. Resume and Up Next keep their songs across rescans. rippl-sync's library isn't read until Tide replaces it.
