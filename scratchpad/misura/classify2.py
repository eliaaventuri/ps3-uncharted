import sys, collections
from PIL import Image
# classify2.py <png...>: come classify.py, piu' "ombra_alto" = frazione di pixel color sfondo nel quarto alto del fotogramma (sotto la barra della finestra)
for f in sys.argv[1:]:
    im = Image.open(f).convert('RGB'); w, h = im.size
    im2 = im.crop((0, int(h * 0.08), w, h)).resize((160, 90))
    px = list(im2.getdata()); n = len(px)
    c = collections.Counter((p[0] // 16, p[1] // 16, p[2] // 16) for p in px)
    sh = c[(3, 3, 4)] / n; bk = c[(0, 0, 0)] / n; wh = sum(v for k, v in c.items() if min(k) >= 14) / n
    top = px[:160 * 22]; ct = collections.Counter((p[0] // 16, p[1] // 16, p[2] // 16) for p in top)
    st = ct[(3, 3, 4)] / len(top)
    tag = 'DIFETTO' if sh > 0.45 or bk > 0.40 or wh > 0.40 or st > 0.25 else 'ok'
    print(f"{f.split('/')[-1]}: ombra {sh:.2f} alto {st:.2f} nero {bk:.2f} bianco {wh:.2f} {tag}")
