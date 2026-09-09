"""Convert 1-bpp PBM screen dumps from the simulator into PNGs styled like the Sharp memory LCD
(3x scale, reflective grey-white ground, near-black pixels, thin bezel)."""
import sys, os
from PIL import Image
for p in sys.argv[1:]:
    im = Image.open(p).convert("L")
    w, h = im.size
    out = Image.new("RGB", (w, h), (226, 229, 224))
    px = im.load(); o = out.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:  # PBM: 1 = black -> PIL L: 0
                o[x, y] = (28, 30, 34)
    out = out.resize((w * 3, h * 3), Image.NEAREST)
    frame = Image.new("RGB", (w * 3 + 24, h * 3 + 24), (60, 62, 66))
    frame.paste(out, (12, 12))
    dst = os.path.splitext(p)[0] + ".png"
    frame.save(dst)
    print("wrote", dst)
