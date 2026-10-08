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

## In-game results (2026-10-07, Skyrim 1.7.104)
- All 11 hooks ok once data_loaded waits for the Main Menu (the loading spinner was too early: DLC
  music types, sound categories and Calendar weren't loaded yet).
- Seen in log/screens: main menu -> NV MainTitle (Skyrim's internal `NoMusic` object there);
  cart -> MainTitle; Helgen -> dungeon pool; dragon attack -> boss (BTTL Evil); radio on ->
  "Radio New Vegas: on" HUD notice, Mr. New Vegas hello, songs; radio off -> situation music back;
  Music slider change 0.50 -> 0.65 followed. Input sink fires every frame (600/600 empty calls).
- Launching SkyrimSE.exe directly needs SteamAppId=489830 in the env or Steam opens the launcher.
- Smart App Control is ON here and blocks unsigned new builds when Defender's cloud lookup fails
  (event 3118, DefenderMadeCloudCall=false). The router DNS (192.168.1.1) drops out; WLANExt.exe
  (Wi-Fi driver ext) crashed 12:34-12:36. Relinking (new hash) with DNS up -> allowed.
  Player risk: SAC users offline may be blocked; code signing would fix (costs money, ask user).

## OPEN: silent exit on save load
- Twice the game exited with no WER/crash record right after a save load started (log ends with
  "skyrim music: (none)"): 12:25 (user's session, after a quicksave) and 12:45 (Continue ->
  Courier 6 quicksave). Not yet known whether the mod causes it. Next: load the same save with the
  .asi removed; if it loads, add a crash log (SetUnhandledExceptionFilter + minidump) and suspect
  worker-thread reads of BSMusicManager/Calendar/sound categories during the load.

## Test-state housekeeping
- Restored after testing: SkyCraft.asi renamed back; MojaveInSkyrim.asi/.ini removed from the game
  folder; SkyrimPrefs.ini back to the game-created fullscreen copy.
- Saves backup: MergedGames\_backup\SkyrimSaves-2026-10-07 (33 files; 3 new saves since, none
  overwritten).

## Next
- Resolve the save-load exit, then: day/night, alt-tab fade, record clip, flip `verified` in
  design/game_hooks.json with evidence, licence decision (CommonLibSSE-NG is GPL-3.0+ with
  exceptions -> mod source must be GPL-compatible and published), Address Library official copy,
  package + Melty recipe (primary skyrim-se, companion fallout-new-vegas, loader
  ultimate-asi-loader, launch {game}/SkyrimSE.exe with MOJAVEINSKYRIM_FNV={game:fallout-new-vegas}).
