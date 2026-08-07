"""Compare two screens as ink/no-ink over the 1024x800 display area.

Ours is an indexed PNG of the framebuffer; the oracle's is MAME's rendered
snapshot, RGB and with the layout artwork below the screen. Only the display
area is comparable, and only as "is this pixel lit" -- the palettes are the
Bt458's on one side and MAME's own on the other.
"""
import sys, zlib, struct

def read(path):
    d = open(path, 'rb').read()
    i, idat, pal = 8, b'', None
    while i < len(d):
        ln = struct.unpack('>I', d[i:i+4])[0]
        typ, data = d[i+4:i+8], d[i+8:i+8+ln]
        i += 12 + ln
        if typ == b'IHDR': w, h, bd, ct = struct.unpack('>IIBB', data[:10])
        elif typ == b'PLTE': pal = data
        elif typ == b'IDAT': idat += data
    raw = zlib.decompress(idat)
    bpp = {0: 1, 2: 3, 3: 1, 6: 4}[ct]
    assert bd == 8, bd
    stride = w * bpp
    out = bytearray(h * stride); prev = bytearray(stride); pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        if f == 1:
            for x in range(bpp, stride): line[x] = (line[x] + line[x-bpp]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + (a + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y*stride:(y+1)*stride] = line; prev = line
    def lit(x, y):
        o = (y * w + x) * bpp
        if ct == 3:
            e = out[o] * 3
            return (pal[e] + pal[e+1] + pal[e+2]) != 0
        if ct == 2:
            return (out[o] + out[o+1] + out[o+2]) != 0
        return out[o] != 0
    return w, h, lit

wa, ha, a = read(sys.argv[1])
wb, hb, b = read(sys.argv[2])
W, H = 1024, 800
diff = [(x, y) for y in range(H) for x in range(W) if a(x, y) != b(x, y)]
ink_a = sum(1 for y in range(H) for x in range(W) if a(x, y))
ink_b = sum(1 for y in range(H) for x in range(W) if b(x, y))
print("ink %s=%d %s=%d  differing pixels=%d of %d"
      % (sys.argv[1].split('/')[-1], ink_a, sys.argv[2].split('/')[-1], ink_b,
         len(diff), W*H))
if diff:
    print("first ten:", diff[:10])
