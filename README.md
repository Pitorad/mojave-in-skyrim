# Mojave in Skyrim

Skyrim Special Edition, scored by Fallout: New Vegas. Single player.

- **The Mojave bleeds in.** Skyrim still decides what kind of music a moment needs (wilderness,
  town, dungeon, combat, boss, discovery, death), and New Vegas answers it from your own copy: desert
  and mountain exploration tracks in the wilds, Freeside and settlement music in towns, vault and
  cave music underground, New Vegas battle loops in fights, and its stingers for discoveries, rewards
  and death. New Vegas's day and night versions follow Skyrim's clock. The cart ride at the start of
  a new game plays New Vegas's main title.
- **Radio New Vegas.** Press **N** to switch it on or off. It plays the station's songs from your
  copy of New Vegas, with Mr. New Vegas saying hello, introducing songs and reading the news between
  them. A notice shows each song's title.
- Story moments (the Word Wall chant, Sovngarde, the Elder Scroll, Alduin's defeat and a few more)
  keep Skyrim's own music.
- Your Music and Master volume sliders still apply. Settings (key, loudness, how often the news
  comes on, day/night hours) are in `MojaveInSkyrim.ini` next to `SkyrimSE.exe`.

## Needs
- The Elder Scrolls V: Skyrim Special Edition (with Ultimate ASI Loader, which Melty installs).
- Fallout: New Vegas installed on the same PC. Nothing from it is shipped: every song and line is
  read from your own install while Skyrim runs. Melty tells the mod where it is; otherwise it looks
  for the install New Vegas registered and in your Steam libraries, or set `sFalloutNVPath` in the
  INI.

## How it works
A CommonLibSSE-NG plugin loaded as `MojaveInSkyrim.asi` (no SKSE needed). It watches which music
type Skyrim's music manager picks, silences Skyrim's music category (form data only, never your
saved slider), and plays New Vegas's audio through its own output with miniaudio. New Vegas's music
is read from `Data\Music`; the radio from `Fallout - Sound.bsa` and `Fallout - Voices1.bsa`.

## Building
The design lives in `design/*.json` sheets (one row per thing). `python tools/sheets.py preflight`
checks every cell, every cross-sheet reference and every row against your installed games;
`python tools/sheets.py gen` writes `src/generated/Sheets.h` and the INI. CMake runs gen itself.

```
cmake --preset default
cmake --build --preset release
```
Needs Visual Studio 2022 Build Tools (C++), vcpkg next to the project in `../.tools/vcpkg`, and
Python 3.

## Credits
- Fallout: New Vegas music, songs and Mr. New Vegas (Wayne Newton) are Bethesda's / Obsidian's and
  their licensors'; they are read from the player's own copy and never distributed.
- CommonLibSSE-NG, miniaudio, stb_vorbis, spdlog, {fmt}, SimpleIni: see `THIRD-PARTY-NOTICES.md`.
- Not affiliated with or endorsed by Bethesda, ZeniMax or Obsidian.
