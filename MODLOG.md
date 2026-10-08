# MODLOG — Mojave in Skyrim

Skyrim SE (host) with Fallout: New Vegas (companion): New Vegas's own music follows Skyrim's music
choice, and Radio New Vegas (songs + Mr. New Vegas) plays on a key. Single player. Everything from
New Vegas is read from the player's own install at run time; nothing from either game is shipped.

## Machine (2026-10-07)
- Skyrim SE 1.7.104.0, Steam, `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition`
  - Already has Ultimate ASI Loader (`dinput8.dll`) and Address Library bins (from SkyCraft via Melty),
    plus `SkyCraft.asi` (SkyCraft mashup installed).
- Fallout: New Vegas 1.4.0.525, Steam, `...\common\Fallout New Vegas` (all DLC).
- Tools installed: Git 2.55, Python 3.12.10 (+lz4 for reading Skyrim BSAs in recon), VS 2022 Build
  Tools 17.14 (MSVC 14.44, CMake, Ninja), vcpkg (`MergedGames\.tools\vcpkg`).

## Route
- Skyrim side: CommonLibSSE-NG plugin built as **`.asi`** (Ultimate ASI Loader, which Melty installs
  for every player). SKSE64 isn't auto-installed by Melty on 1.7.104, so no SKSE dependency.
  Boot per Melty's game_info: DllMain starts a thread; thread waits for `RE::UI`, then a
  MenuOpenCloseEvent sink detects "data loaded" (first event once `LookupByID(0x7)` exists).
- No game hooks/trampolines: reads `BSMusicManager::current`, writes the music sound category's
  static multiplier, adds input/menu event sinks.
- Audio: miniaudio (+stb_vorbis) on its own WASAPI output. New Vegas music is loose MP3
  (`Data\Music`), radio songs are OGG in `Fallout - Sound.bsa` (`sound\songs\radionv`), Mr. New Vegas
  is OGG in `Fallout - Voices1.bsa` (`sound\voice\falloutnv.esm\maleuniquemrnewvegas`). All
  uncompressed (checked), so the BSA reader needs no zlib.

## Facts found
- Skyrim picks music by MUSC "music type"; 51 in Skyrim.esm, +2 Update, +5 Dawnguard, +7 Dragonborn.
  Sheet `skyrim_music_types` maps each to a New Vegas pool; preflight checks EDIDs against the ESMs.
- `AudioCategoryMUS` = 0x071E64 (VNAM static mult 0xB332), `_AudioCategoryMaster` = 0x0EB803.
- CommonLib bug: `BGSSoundCategory::GetStaticVolumeMultiplier` does integer division; we save/restore
  the raw `staticMult` instead.
- No `DebugNotification` in this CommonLib; `RE::SendHUDMessage::ShowHUDMessage` does the same.
- Free keys in Skyrim's default controlmap (from Skyrim - Interface.bsa): Y U G H K B N. Radio = N (0x31).
- New Vegas LOC music has `_Day_`/`_Night_` variants; DNGN/BTTL don't.

## Sheets → code
`design/*.json` → `python tools/sheets.py preflight` → `python tools/sheets.py gen` writes
`src/generated/Sheets.h` + `package/MojaveInSkyrim.ini`. CMake reruns gen when a sheet changes.
`/we4062` makes a switch over a sheet enum fail the build if a row isn't handled.

## Next
- First build; then in-game test: hooks log, music swap, radio key, notices. Flip `verified` in
  `design/game_hooks.json` with evidence.
