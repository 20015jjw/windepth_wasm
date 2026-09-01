# Nearest-neighbour zoom of an indexed PNG produced by dump_dar.py, so small
# sprites are legible when read back.
import struct, zlib, sys
def readpng(p):
    d=open(p,'rb').read(); o=8; w=h=None; pal=None; idat=b''
    while o<len(d):
        ln,=struct.unpack_from('>I',d,o); t=d[o+4:o+8]; b=d[o+8:o+8+ln]
        if t==b'IHDR': w,h=struct.unpack_from('>II',b,0)
        elif t==b'PLTE': pal=[tuple(b[i:i+3]) for i in range(0,len(b),3)]
        elif t==b'IDAT': idat+=b
        o+=12+ln
    raw=zlib.decompress(idat)
    rows=[raw[y*(w+1)+1:(y+1)*(w+1)] for y in range(h)]
    return w,h,pal,rows
def writepng(p,w,h,pal,rows):
    raw=b''.join(b'\0'+bytes(r) for r in rows)
    def ck(t,b):
        c=t+b; return struct.pack('>I',len(b))+c+struct.pack('>I',zlib.crc32(c))
    open(p,'wb').write(b'\x89PNG\r\n\x1a\n'+ck(b'IHDR',struct.pack('>IIBBBBB',w,h,8,3,0,0,0))
        +ck(b'PLTE',b''.join(bytes(c) for c in pal))+ck(b'IDAT',zlib.compress(raw))+ck(b'IEND',b''))
src,dst,z=sys.argv[1],sys.argv[2],int(sys.argv[3])
w,h,pal,rows=readpng(src)
out=[]
for y in range(h):
    r=bytes(b for px in rows[y] for b in [px]*z)
    for _ in range(z): out.append(r)
writepng(dst,w*z,h*z,pal,out)
