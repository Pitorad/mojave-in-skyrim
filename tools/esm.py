"""Minimal reader for Bethesda plugin files (Skyrim SE TES4 v1.7 headers; also FO3/FNV, which share the layout).

Used by the preflight to check that every form ID in the design sheets exists in the player's own
Skyrim.esm with the expected record type and editor ID. Reads only what it is asked for.
"""
import struct
import zlib

REC_HDR = 24  # type, size, flags, formid, vc info (8)
COMPRESSED = 0x00040000


def _subrecords(data):
    i = 0
    big = None
    while i + 6 <= len(data):
        sig = data[i:i + 4].decode('latin1')
        size = struct.unpack_from('<H', data, i + 4)[0]
        i += 6
        if sig == 'XXXX':
            big = struct.unpack_from('<I', data, i)[0]
            i += size
            continue
        if big is not None:
            size, big = big, None
        yield sig, data[i:i + size]
        i += size


def records(path, wanted):
    """Yield (type, formid, {sub: [bytes...]}) for every record whose type is in `wanted`,
    looking only inside top-level groups of those types."""
    wanted = set(wanted)
    with open(path, 'rb') as f:
        buf = f.read()
    # skip TES4 header record
    size = struct.unpack_from('<I', buf, 4)[0]
    pos = REC_HDR + size
    while pos < len(buf):
        gsize = struct.unpack_from('<I', buf, pos + 4)[0]
        label = buf[pos + 8:pos + 12].decode('latin1')
        if label in wanted:
            yield from _walk(buf, pos + REC_HDR, pos + gsize, wanted)
        pos += gsize


def _walk(buf, pos, end, wanted):
    while pos < end:
        sig = buf[pos:pos + 4].decode('latin1')
        size = struct.unpack_from('<I', buf, pos + 4)[0]
        if sig == 'GRUP':
            yield from _walk(buf, pos + REC_HDR, pos + size, wanted)
            pos += size
            continue
        flags, formid = struct.unpack_from('<II', buf, pos + 8)
        data = buf[pos + REC_HDR:pos + REC_HDR + size]
        pos += REC_HDR + size
        if sig not in wanted:
            continue
        if flags & COMPRESSED:
            data = zlib.decompress(data[4:])
        subs = {}
        for s, d in _subrecords(data):
            subs.setdefault(s, []).append(d)
        yield sig, formid, subs


def zstr(b):
    return b.split(b'\0', 1)[0].decode('latin1')
