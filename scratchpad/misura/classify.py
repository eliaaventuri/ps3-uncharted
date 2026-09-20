import sys, collections
from PIL import Image
# classify.py <png...>: frazione di pixel "ombra" (3,3,4), neri e bianchi; DIFETTO se ombra>45% o nero>40% o bianco>40%
for f in sys.argv[1:]:
    im = Image.open(f).convert('RGB'); w, h = im.size
    im2 = im.crop((0, int(h * 0.08), w, h)).resize((160, 90))
    px = list(im2.getdata()); n = len(px)
    c = collections.Counter((p[0] // 16, p[1] // 16, p[2] // 16) for p in px)
    sh = c[(3, 3, 4)] / n; bk = c[(0, 0, 0)] / n; wh = sum(v for k, v in c.items() if min(k) >= 14) / n
    print(f"{f}: ombra {sh:.2f} nero {bk:.2f} bianco {wh:.2f} {'DIFETTO' if sh > 0.45 or bk > 0.40 or wh > 0.40 else 'ok'}")
