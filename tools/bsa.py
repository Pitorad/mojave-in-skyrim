"""List the files in a Fallout 3 / New Vegas (v104) or Skyrim SE (v105) BSA archive."""
import struct


def list_files(path):
    """Return {r'folder\\file': size} with lower-case names."""
    with open(path, 'rb') as f:
        magic, ver, _off, _flags, nfold, _nfile, _tfold, tfile, _ff = struct.unpack('<4sIIIIIIII', f.read(36))
        if magic != b'BSA\0':
            raise ValueError(f'{path} is not a BSA')
        rec = '<QIIQ' if ver == 105 else '<QII'
        counts = [struct.unpack(rec, f.read(struct.calcsize(rec)))[1] for _ in range(nfold)]
        entries = []
        for cnt in counts:
            ln = f.read(1)[0]
            folder = f.read(ln)[:-1].decode('latin1').lower()
            for _ in range(cnt):
                _h, size, _o = struct.unpack('<QII', f.read(16))
                entries.append((folder, size & 0x3FFFFFFF))
        names = f.read(tfile).split(b'\0')
    return {fo + '\\' + names[i].decode('latin1').lower(): size for i, (fo, size) in enumerate(entries)}
