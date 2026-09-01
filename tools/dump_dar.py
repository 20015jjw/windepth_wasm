# Walk WIND_DAR and dump every pattern.  Format taken from FUN_0040f590/FUN_0040f850.
#
#   "DAR:8" magic, byte 5 = version (<=1), uint16 at 6 = pattern count,
#   256 RGBQUAD palette at 0x0c, then `count` records from 0x40c:
#
#     uint16 reclen        bytes after this field up to the pixel data
#     uint16 w             -> struct+0x10
#     uint16 h             -> struct+0x12 / +0x16, rows of pixel data
#     uint16 pitch         -> struct+0x1c, bytes per row
#     int16  transparent   -> struct+0x20, -1 = opaque
#     [reclen>13]  uint16 x3, uint16 x3b, uint16 h2 (overwrites +0x12)
#     [reclen>15]  uint8 namefield, char name[namefield-1]
#   pixel data = h * pitch bytes
import struct, sys, os

d = open(sys.argv[1] if len(sys.argv) > 1 else 'orig/res/DAR_WIND_DAR.bin', 'rb').read()
assert d[:5] == b'DAR:8', d[:8]
ver = d[5]
count = struct.unpack_from('<H', d, 6)[0]
pal = [(d[0x0c+4*i+2], d[0x0c+4*i+1], d[0x0c+4*i+0]) for i in range(256)]
print('ver', ver, 'count', count, 'hdr8', struct.unpack_from('<HH', d, 8))

o = 0x40c
recs = []
for i in range(count):
    reclen = struct.unpack_from('<H', d, o)[0]
    b = o + 2
    w, h, pitch = struct.unpack_from('<HHH', d, b)
    trans, = struct.unpack_from('<h', d, b + 6)
    x3 = x3b = h2 = None
    name = ''
    if reclen > 13:
        x3, x3b, h2 = struct.unpack_from('<HHH', d, b + 8)
    if reclen > 15:
        nf = d[b + 14]
        name = d[b+15:b+15+nf-1].split(b'\0')[0].decode('ascii', 'replace')
    data = b + reclen
    size = h * pitch
    recs.append(dict(i=i, o=o, reclen=reclen, w=w, h=h, pitch=pitch, trans=trans,
                     x3=x3, x3b=x3b, h2=h2, name=name, data=data, size=size))
    o = b + reclen + size
print('walked to', hex(o), 'of', hex(len(d)), 'OK' if o == len(d) else 'MISMATCH')

if '--png' in sys.argv:
    import zlib
    os.makedirs('tests/dar', exist_ok=True)
    def png(path, w, h, rows):
        raw = b''.join(b'\0' + bytes(rows[y]) for y in range(h))
        def chunk(t, b):
            c = t + b
            return struct.pack('>I', len(b)) + c + struct.pack('>I', zlib.crc32(c))
        hdr = struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0)
        plte = b''.join(bytes(c) for c in pal)
        open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', hdr) +
                               chunk(b'PLTE', plte) + chunk(b'IDAT', zlib.compress(raw)) +
                               chunk(b'IEND', b''))
    for r in recs:
        rows = [d[r['data']+y*r['pitch'] : r['data']+y*r['pitch']+r['pitch']] for y in range(r['h'])]
        nm = r['name'] or 'p%03d' % r['i']
        png('tests/dar/%03d_%s.png' % (r['i'], nm), r['pitch'], r['h'], rows)

for r in recs:
    print('%3d @%06x len%3d  w%4d h%4d pitch%4d tr%5d  x3=%s x3b=%s h2=%s  %s' % (
        r['i'], r['o'], r['reclen'], r['w'], r['h'], r['pitch'], r['trans'],
        r['x3'], r['x3b'], r['h2'], r['name']))
