import struct

d = open('/home/wineuser/build/wine64/programs/explorer/explorer.res', 'rb').read()
pos = 32
while pos < len(d):
    data_size, hdr_size = struct.unpack('<II', d[pos:pos+8])
    hdr = d[pos+8:pos+hdr_size]
    hpos = 0
    t_val = struct.unpack('<H', hdr[hpos:hpos+2])[0]
    if t_val == 0xffff:
        t_id = struct.unpack('<H', hdr[hpos+2:hpos+4])[0]
        hpos += 4
    else:
        t_id = 'str'
        while hdr[hpos:hpos+2] != b'\x00\x00': hpos += 2
        hpos += 2
    if hpos % 4 != 0: hpos += 2
    n_val = struct.unpack('<H', hdr[hpos:hpos+2])[0]
    if n_val == 0xffff:
        n_id = struct.unpack('<H', hdr[hpos+2:hpos+4])[0]
        hpos += 4
    else:
        s = []
        while hdr[hpos:hpos+2] != b'\x00\x00':
            s.append(chr(struct.unpack('<H', hdr[hpos:hpos+2])[0]))
            hpos += 2
        n_id = ''.join(s)
        hpos += 2
    if t_id == 2:
        print(f"BITMAP: name={n_id}")
    pos += hdr_size + data_size
    if pos % 4 != 0: pos += 4 - (pos % 4)
