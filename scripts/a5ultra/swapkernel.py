import struct, hashlib, sys
# usage: swapkernel.py orig.img Image dtb.img out.img
orig, kimg, dtimg, out = sys.argv[1:5]
d = open(orig, 'rb').read()
assert d[:8] == b'ANDROID!'
(ks, ka, rs, ra, ss, sa, ta, ps, dts, osv) = struct.unpack_from('<10I', d, 8)
def pages(n): return (n + ps - 1) // ps
off = ps
kernel = d[off:off+ks]; off += pages(ks)*ps
ramdisk = d[off:off+rs]; off += pages(rs)*ps
second = d[off:off+ss]; off += pages(ss)*ps
dt = d[off:off+dts]; off += pages(dts)*ps
tail = d[off:]
print('orig: kernel %d ramdisk %d second %d dt %d tail %r' % (ks, rs, ss, dts, tail[:16]))
nk = open(kimg, 'rb').read(); nd = open(dtimg, 'rb').read()
h = bytearray(d[:ps])
struct.pack_into('<I', h, 8, len(nk)); struct.pack_into('<I', h, 8+4*8, len(nd))
sha = hashlib.sha1()
for blob in (nk, ramdisk, second, nd):
    sha.update(blob); sha.update(struct.pack('<I', len(blob)))
# id field at offset 8+40+16+512 = 576
h[576:576+32] = sha.digest() + b'\0'*12
def pad(b): return b + b'\0'*((-len(b)) % ps)
img = bytes(h) + pad(nk) + pad(ramdisk) + (pad(second) if ss else b'') + pad(nd) + b'SEANDROIDENFORCE'
open(out, 'wb').write(img)
print('new : kernel %d ramdisk %d dt %d -> %s (%d bytes)' % (len(nk), len(ramdisk), len(nd), out, len(img)))
