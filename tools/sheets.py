"""Design sheets -> preflight report and generated C++.

    python tools/sheets.py preflight [--release] [--skyrim DIR] [--fnv DIR]
    python tools/sheets.py gen

preflight lays every sheet over the others and over the player's own game files and lists each
unfilled cell, each reference that does not resolve and each hook not yet proven in game. It exits 1
when anything blocks a build (and, with --release, when any hook is still unverified).
gen writes src/generated/Sheets.h and package/MojaveInSkyrim.ini from the sheets; it refuses to run
unless the preflight has no errors.
"""
import argparse
import json
import os
import re
import struct
import sys
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN = ROOT / 'design'
sys.path.insert(0, str(Path(__file__).parent))
import bsa  # noqa: E402
import esm  # noqa: E402

SHEETS = ['fnv_sources', 'music_pools', 'skyrim_music_types', 'radio_songs', 'radio_dj', 'settings', 'game_hooks']

# column -> (type, may be empty)
SCHEMA = {
    'fnv_sources': {'id': (str, False), 'path': (str, False), 'kind': (str, False), 'inner': (str, True), 'usedBy': (str, False)},
    'music_pools': {'id': (str, False), 'label': (str, False), 'sources': (list, True), 'dayNight': (bool, False), 'repeat': (str, False)},
    'skyrim_music_types': {'plugin': (str, False), 'formId': (str, False), 'editorId': (str, False), 'pool': (str, False)},
    'radio_songs': {'file': (str, False), 'title': (str, False)},
    'radio_dj': {'id': (str, False), 'prefix': (str, False), 'role': (str, False)},
    'settings': {'section': (str, False), 'key': (str, False), 'type': (str, False), 'default': (object, True),
                 'min': (object, True), 'max': (object, True), 'about': (str, False)},
    'game_hooks': {'id': (str, False), 'api': (str, False), 'thread': (str, False), 'access': (str, False),
                   'purpose': (str, False), 'verified': (bool, False), 'evidence': (str, True)},
}
ENUMS = {
    ('fnv_sources', 'kind'): {'folder', 'bsa'},
    ('music_pools', 'repeat'): {'loop', 'once'},
    ('radio_dj', 'role'): {'switch_on', 'before_song', 'news_open', 'news_story', 'news_close'},
    ('settings', 'type'): {'bool', 'int', 'float', 'string', 'pool'},
    ('game_hooks', 'thread'): {'main', 'worker', 'any'},
    ('game_hooks', 'access'): {'read', 'write', 'sink'},
}
SKYRIM_MASTERS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'HearthFires.esm', 'Dragonborn.esm']
# pools that play nothing of New Vegas's by design
NO_SOURCE_POOLS = {'silence', 'skyrim'}


def load():
    return {name: json.loads((DESIGN / f'{name}.json').read_text(encoding='utf-8')) for name in SHEETS}


def steam_game(folder):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            steam = Path(winreg.QueryValueEx(k, 'SteamPath')[0])
    except OSError:
        return None
    vdf = steam / 'steamapps' / 'libraryfolders.vdf'
    libs = [steam]
    if vdf.exists():
        libs += [Path(p.replace('\\\\', '\\')) for p in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8'))]
    for lib in libs:
        p = lib / 'steamapps' / 'common' / folder
        if p.exists():
            return p
    return None


class Report:
    def __init__(self):
        self.errors, self.unchecked, self.notes = [], [], []

    def err(self, where, msg):
        self.errors.append(f'{where}: {msg}')


def check_cells(sheets, r):
    for name, sheet in sheets.items():
        cols = SCHEMA[name]
        if set(sheet.get('columns', {})) != set(cols):
            r.err(name, f'declared columns {sorted(sheet.get("columns", {}))} differ from schema {sorted(cols)}')
        for i, row in enumerate(sheet['rows']):
            where = f'{name}[{i}]'
            for col, (typ, may_empty) in cols.items():
                if col not in row:
                    r.err(where, f'cell "{col}" is missing')
                    continue
                v = row[col]
                if typ is not object and not isinstance(v, typ):
                    r.err(where, f'"{col}" should be {typ.__name__}, is {v!r}')
                    continue
                if not may_empty and (v == '' or v is None or v == []):
                    r.err(where, f'"{col}" is empty')
                allowed = ENUMS.get((name, col))
                if allowed and v not in allowed:
                    r.err(where, f'"{col}" = {v!r} is not one of {sorted(allowed)}')
            for extra in set(row) - set(cols):
                r.err(where, f'unknown column "{extra}"')


def check_unique(sheets, r):
    keys = {'fnv_sources': 'id', 'music_pools': 'id', 'radio_songs': 'file', 'radio_dj': 'id', 'game_hooks': 'id'}
    for name, key in keys.items():
        seen = set()
        for row in sheets[name]['rows']:
            if row.get(key) in seen:
                r.err(name, f'duplicate {key} {row.get(key)!r}')
            seen.add(row.get(key))
    seen = set()
    for row in sheets['skyrim_music_types']['rows']:
        k = (row['plugin'].lower(), row['formId'].upper())
        if k in seen:
            r.err('skyrim_music_types', f'duplicate {k}')
        seen.add(k)
    seen = set()
    for row in sheets['settings']['rows']:
        k = (row['section'], row['key'])
        if k in seen:
            r.err('settings', f'duplicate {k}')
        seen.add(k)


def check_refs(sheets, r):
    pools = {p['id'] for p in sheets['music_pools']['rows']}
    used = set()
    for i, row in enumerate(sheets['skyrim_music_types']['rows']):
        if row['pool'] not in pools:
            r.err(f'skyrim_music_types[{i}] {row["editorId"]}', f'pool {row["pool"]!r} is not in music_pools')
        used.add(row['pool'])
        if not re.fullmatch(r'[0-9A-Fa-f]{6}', row['formId']):
            r.err(f'skyrim_music_types[{i}]', f'formId {row["formId"]!r} is not 6 hex digits')
    for i, s in enumerate(sheets['settings']['rows']):
        where = f'settings[{i}] [{s["section"]}] {s["key"]}'
        t, d = s['type'], s['default']
        prefix = {'bool': 'b', 'int': 'i', 'float': 'f', 'string': 's', 'pool': 's'}.get(t)
        if prefix and not s['key'].startswith(prefix):
            r.err(where, f'key prefix should be "{prefix}" for type {t}')
        if t == 'pool':
            if d not in pools:
                r.err(where, f'default pool {d!r} is not in music_pools')
            used.add(d)
        if t in ('int', 'float'):
            if not isinstance(d, (int, float)) or isinstance(d, bool):
                r.err(where, 'numeric default required')
            elif s['min'] is None or s['max'] is None:
                r.err(where, 'numeric setting needs min and max')
            elif not s['min'] <= d <= s['max']:
                r.err(where, f'default {d} outside {s["min"]}..{s["max"]}')
        else:
            if s['min'] is not None or s['max'] is not None:
                r.err(where, 'min/max only apply to numbers')
            if t == 'bool' and not isinstance(d, bool):
                r.err(where, 'bool default required')
            if t in ('string', 'pool') and not isinstance(d, str):
                r.err(where, 'string default required')
    for p in sheets['music_pools']['rows']:
        if p['id'] not in used:
            r.notes.append(f'music_pools {p["id"]}: not used by any music type or setting')
        if not p['sources'] and p['id'] not in NO_SOURCE_POOLS:
            r.err(f'music_pools {p["id"]}', 'no sources')
        if p['sources'] and p['id'] in NO_SOURCE_POOLS:
            r.err(f'music_pools {p["id"]}', 'must have no sources')
    roles = {d['role'] for d in sheets['radio_dj']['rows']}
    for role in ENUMS[('radio_dj', 'role')] - roles:
        r.err('radio_dj', f'no category for role {role}')
    src_ids = {s['id'] for s in sheets['fnv_sources']['rows']}
    for need in ('music', 'songs', 'voices'):
        if need not in src_ids:
            r.err('fnv_sources', f'missing source {need!r} the code reads')
    for s in sheets['fnv_sources']['rows']:
        if s['usedBy'] not in SHEETS:
            r.err(f'fnv_sources {s["id"]}', f'usedBy {s["usedBy"]!r} is not a sheet')
        if (s['kind'] == 'bsa') != bool(s['inner']):
            r.err(f'fnv_sources {s["id"]}', 'inner is required for bsa and must be empty for folder')


def check_skyrim(sheets, r, skyrim):
    data = skyrim / 'Data'
    found = {}
    for plugin in SKYRIM_MASTERS:
        p = data / plugin
        if not p.exists():
            r.notes.append(f'{plugin} not installed here; its rows are checked by name only')
            continue
        for _t, fid, subs in esm.records(p, ['MUSC']):
            if (fid >> 24) != _master_index(p):
                continue  # an override of a record from an earlier master
            found[(plugin.lower(), f'{fid & 0xFFFFFF:06X}')] = esm.zstr(subs['EDID'][0])
    rows = {(row['plugin'].lower(), row['formId'].upper()): row for row in sheets['skyrim_music_types']['rows']}
    for key, row in rows.items():
        if (data / row['plugin']).exists():
            if key not in found:
                r.err(f'skyrim_music_types {row["editorId"]}', f'{row["plugin"]} has no MUSC {row["formId"]}')
            elif found[key] != row['editorId']:
                r.err(f'skyrim_music_types {row["formId"]}', f'editorId is {found[key]!r} in the game, sheet says {row["editorId"]!r}')
    for key, edid in found.items():
        if key not in rows:
            r.err('skyrim_music_types', f'{key[0]} {key[1]} {edid} has no row (would fall back to the unknown pool)')


def _master_index(path):
    """Index of the file's own records in its form IDs = its number of masters."""
    with open(path, 'rb') as f:
        head = f.read(24)
        size = struct.unpack_from('<I', head, 4)[0]
        data = f.read(size)
    return sum(1 for s, _ in esm._subrecords(data) if s == 'MAST')


def check_fnv(sheets, r, fnv):
    srcs = {s['id']: s for s in sheets['fnv_sources']['rows']}
    archives = {}
    for s in srcs.values():
        p = fnv / s['path']
        if not p.exists():
            r.err(f'fnv_sources {s["id"]}', f'{p} does not exist')
        elif s['kind'] == 'bsa':
            archives[s['id']] = bsa.list_files(p)
    music = fnv / srcs['music']['path']
    for pool in sheets['music_pools']['rows']:
        files = []
        for src in pool['sources']:
            p = music / src
            if p.is_dir():
                files += [f.name for f in p.iterdir() if f.suffix.lower() == '.mp3']
            elif p.is_file() and p.suffix.lower() == '.mp3':
                files.append(p.name)
            else:
                r.err(f'music_pools {pool["id"]}', f'source {src!r} is not an .mp3 or a folder of them')
        if pool['sources'] and not files:
            r.err(f'music_pools {pool["id"]}', 'no .mp3 files found')
        if pool['dayNight']:
            day = [f for f in files if '_day_' in f.lower()]
            night = [f for f in files if '_night_' in f.lower()]
            if not day or not night or len(day) + len(night) != len(files):
                r.err(f'music_pools {pool["id"]}', f'dayNight needs every file to be _Day_ or _Night_ ({len(day)} day, {len(night)} night, {len(files)} total)')
    if 'songs' in archives:
        inner = srcs['songs']['inner'].lower()
        for song in sheets['radio_songs']['rows']:
            if f'{inner}\\{song["file"].lower()}' not in archives['songs']:
                r.err(f'radio_songs {song["title"]}', f'{song["file"]} is not in {srcs["songs"]["path"]}')
    if 'voices' in archives:
        inner = srcs['voices']['inner'].lower()
        for dj in sheets['radio_dj']['rows']:
            pat = re.compile(re.escape(f'{inner}\\{dj["prefix"].lower()}') + r'_[0-9a-f]{8}_\d+\.ogg$')
            n = sum(1 for k in archives['voices'] if pat.match(k))
            if n == 0:
                r.err(f'radio_dj {dj["id"]}', f'no lines match {dj["prefix"]}_<formid>_<n>.ogg')
            else:
                r.notes.append(f'radio_dj {dj["id"]}: {n} lines')


def check_hooks(sheets, r):
    for h in sheets['game_hooks']['rows']:
        if not h['verified']:
            r.unchecked.append(f'game_hooks {h["id"]}: not yet proven in the running game ({h["purpose"]})')
        elif not h['evidence']:
            r.err(f'game_hooks {h["id"]}', 'verified without evidence')


def preflight(args):
    sheets = load()
    r = Report()
    check_cells(sheets, r)
    check_unique(sheets, r)
    cells_ok = not r.errors  # the later checks index cells the schema check guarantees
    if cells_ok:
        check_refs(sheets, r)
    skyrim = Path(args.skyrim) if args.skyrim else steam_game('Skyrim Special Edition')
    fnv = Path(args.fnv) if args.fnv else steam_game('Fallout New Vegas')
    if cells_ok:
        if skyrim:
            check_skyrim(sheets, r, skyrim)
        else:
            r.err('preflight', 'Skyrim Special Edition not found; pass --skyrim')
        if fnv:
            check_fnv(sheets, r, fnv)
        else:
            r.err('preflight', 'Fallout: New Vegas not found; pass --fnv')
    check_hooks(sheets, r)
    cells = sum(len(s['rows']) * len(SCHEMA[n]) for n, s in sheets.items())
    print(f'Preflight: {len(SHEETS)} sheets, {sum(len(s["rows"]) for s in sheets.values())} rows, {cells} cells')
    print(f'  Skyrim: {skyrim}\n  New Vegas: {fnv}')
    for n in r.notes:
        print(f'  note   {n}')
    for u in r.unchecked:
        print(f'  open   {u}')
    for e in r.errors:
        print(f'  ERROR  {e}')
    blocked = bool(r.errors) or (args.release and bool(r.unchecked))
    print('Preflight ' + ('FAILED' if blocked else 'clean' + (f' for a build ({len(r.unchecked)} hooks still to prove in game)' if r.unchecked else '')))
    return 1 if blocked else 0


# ---------------------------------------------------------------- generation

def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def ident(s):
    return re.sub(r'\W', '_', s)


def gen(_args):
    pre = argparse.Namespace(release=False, skyrim=None, fnv=None)
    if preflight(pre):
        print('gen: fix the preflight first')
        return 1
    sh = load()
    out = []
    w = out.append
    w('// Generated by tools/sheets.py from design/*.json. Do not edit: change the sheet, then run gen.')
    w('#pragma once\n')
    w('#include <array>\n#include <cstdint>\n#include <span>\n#include <string_view>\n')
    w('namespace mis::sheets\n{')
    w('\tusing namespace std::string_view_literals;\n')

    # fnv_sources
    w('\tenum class SourceKind { kFolder, kBsa };')
    w('\tstruct FnvSource { std::string_view id; std::string_view path; SourceKind kind; std::string_view inner; };')
    for s in sh['fnv_sources']['rows']:
        w(f'\tinline constexpr FnvSource kSource_{ident(s["id"])}{{ {cstr(s["id"])}, {cstr(s["path"])}, SourceKind::k{s["kind"].capitalize()}, {cstr(s["inner"])} }};')
    w('')

    # music_pools
    pools = sh['music_pools']['rows']
    w('\tenum class Pool : std::uint8_t\n\t{')
    for p in pools:
        w(f'\t\t{ident(p["id"])},')
    w('\t\tkCount\n\t};')
    w('\tenum class Repeat { kLoop, kOnce };')
    w('\tstruct MusicPool { Pool id; std::string_view name; std::string_view label; std::span<const std::string_view> sources; bool dayNight; Repeat repeat; };')
    for p in pools:
        if p['sources']:
            w(f'\tinline constexpr std::array<std::string_view, {len(p["sources"])}> kPoolSources_{ident(p["id"])}{{ ' + ', '.join(cstr(x) for x in p['sources']) + ' };')
    w(f'\tinline constexpr std::array<MusicPool, {len(pools)}> kPools{{{{')
    for p in pools:
        src = f'kPoolSources_{ident(p["id"])}' if p['sources'] else 'std::span<const std::string_view>{}'
        w(f'\t\t{{ Pool::{ident(p["id"])}, {cstr(p["id"])}, {cstr(p["label"])}, {src}, {str(p["dayNight"]).lower()}, Repeat::k{p["repeat"].capitalize()} }},')
    w('\t}};\n')

    # skyrim_music_types
    types = sh['skyrim_music_types']['rows']
    w('\tstruct MusicTypeRow { std::string_view plugin; std::uint32_t localFormId; std::string_view editorId; Pool pool; };')
    w(f'\tinline constexpr std::array<MusicTypeRow, {len(types)}> kMusicTypes{{{{')
    for t in types:
        w(f'\t\t{{ {cstr(t["plugin"])}, 0x{t["formId"].upper()}, {cstr(t["editorId"])}, Pool::{ident(t["pool"])} }},')
    w('\t}};\n')

    # radio_songs
    songs = sh['radio_songs']['rows']
    w('\tstruct RadioSong { std::string_view file; std::string_view title; };')
    w(f'\tinline constexpr std::array<RadioSong, {len(songs)}> kSongs{{{{')
    for s in songs:
        w(f'\t\t{{ {cstr(s["file"])}, {cstr(s["title"])} }},')
    w('\t}};\n')

    # radio_dj
    dj = sh['radio_dj']['rows']
    w('\tenum class DjRole { kSwitchOn, kBeforeSong, kNewsOpen, kNewsStory, kNewsClose };')
    w('\tstruct DjCategory { std::string_view id; std::string_view prefix; DjRole role; };')
    w(f'\tinline constexpr std::array<DjCategory, {len(dj)}> kDj{{{{')
    for d in dj:
        role = ''.join(x.capitalize() for x in d['role'].split('_'))
        w(f'\t\t{{ {cstr(d["id"])}, {cstr(d["prefix"])}, DjRole::k{role} }},')
    w('\t}};\n')

    # game_hooks
    hooks = sh['game_hooks']['rows']
    w('\tenum class Hook : std::uint8_t\n\t{')
    for h in hooks:
        w(f'\t\t{ident(h["id"])},')
    w('\t\tkCount\n\t};')
    w('\tstruct HookInfo { Hook id; std::string_view name; std::string_view api; bool verified; };')
    w(f'\tinline constexpr std::array<HookInfo, {len(hooks)}> kHooks{{{{')
    for h in hooks:
        w(f'\t\t{{ Hook::{ident(h["id"])}, {cstr(h["id"])}, {cstr(h["api"])}, {str(h["verified"]).lower()} }},')
    w('\t}};\n')

    # settings
    ctype = {'bool': 'bool', 'int': 'std::int32_t', 'float': 'float', 'string': 'std::string', 'pool': 'Pool'}
    w('\t// Settings: one field per settings row, named <section>_<key>.')
    w('\tstruct Settings\n\t{')
    for s in sh['settings']['rows']:
        d = s['default']
        if s['type'] == 'bool':
            dv = str(d).lower()
        elif s['type'] == 'float':
            dv = f'{float(d)}f'
        elif s['type'] == 'pool':
            dv = f'Pool::{ident(d)}'
        elif s['type'] == 'string':
            dv = cstr(d)
        else:
            dv = str(d)
        w(f'\t\t{ctype[s["type"]]} {s["section"].lower()}_{s["key"]}{{ {dv} }};')
    w('\t};\n')
    w('\t// Reads every settings row from an INI through a reader with GetBool/GetInt/GetFloat/GetString/GetPool.')
    w('\ttemplate <class Reader>\n\tvoid ReadSettings(const Reader& a_ini, Settings& a_out)\n\t{')
    for s in sh['settings']['rows']:
        f = f'a_out.{s["section"].lower()}_{s["key"]}'
        sec, key = cstr(s['section']), cstr(s['key'])
        if s['type'] == 'bool':
            w(f'\t\t{f} = a_ini.GetBool({sec}, {key}, {f});')
        elif s['type'] == 'int':
            w(f'\t\t{f} = std::clamp<std::int32_t>(a_ini.GetInt({sec}, {key}, {f}), {s["min"]}, {s["max"]});')
        elif s['type'] == 'float':
            w(f'\t\t{f} = std::clamp(a_ini.GetFloat({sec}, {key}, {f}), {float(s["min"])}f, {float(s["max"])}f);')
        elif s['type'] == 'pool':
            w(f'\t\t{f} = a_ini.GetPool({sec}, {key}, {f});')
        else:
            w(f'\t\t{f} = a_ini.GetString({sec}, {key}, {f});')
    w('\t}')
    w('}')
    gen_dir = ROOT / 'src' / 'generated'
    gen_dir.mkdir(parents=True, exist_ok=True)
    text = '\n'.join(out) + '\n'
    text = text.replace('#include <array>', '#include <algorithm>\n#include <array>').replace('#include <span>', '#include <span>\n#include <string>')
    (gen_dir / 'Sheets.h').write_text(text, encoding='utf-8')

    # INI shipped to players
    ini = ['; Mojave in Skyrim: New Vegas music and Radio New Vegas in Skyrim.',
           '; Generated from design/settings.json. Delete a line to get its default back.', '']
    section = None
    for s in sh['settings']['rows']:
        if s['section'] != section:
            section = s['section']
            ini += ['', f'[{section}]']
        ini.append(f'; {s["about"]}')
        d = s['default']
        v = ('1' if d else '0') if s['type'] == 'bool' else str(d)
        ini.append(f'{s["key"]} = {v}')
    pkg = ROOT / 'package'
    pkg.mkdir(exist_ok=True)
    (pkg / 'MojaveInSkyrim.ini').write_text('\n'.join(ini).replace('\n\n\n', '\n\n') + '\n', encoding='utf-8')
    print(f'gen: wrote {gen_dir / "Sheets.h"} and {pkg / "MojaveInSkyrim.ini"}')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['preflight', 'gen'])
    ap.add_argument('--release', action='store_true')
    ap.add_argument('--skyrim')
    ap.add_argument('--fnv')
    a = ap.parse_args()
    return preflight(a) if a.cmd == 'preflight' else gen(a)


if __name__ == '__main__':
    sys.exit(main())
