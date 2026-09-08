#!/usr/bin/env python3
"""Estrae i moduli SPU del job manager WwsJob incorporati in un binario PPU.

`extract_spu_images.py` cerca ELF SPU annidati. I job WwsJob non sono ELF: sono
immagini grezze con un header di 0x30 byte, e in Uncharted ce ne sono 28 dentro
l'EBOOT che nessuno stava guardando. Sono il codice SPU vero del titolo -- fisica,
render, animazione -- e senza di loro ogni bandiera di completamento che il PPU
aspetta resta a zero.

Header (offset da module_start, big-endian):

    +0x00  4 istruzioni `ila`   bootstrap
    +0x10  entryOffset          da module_start
    +0x14  imageSize            da module_start (header incluso)
    +0x18  bssSize
    +0x1C  (non identificato: 0 in 20 moduli su 28)
    +0x20  0xC0DEC0DE
    +0x24  flags                0 / 4 / 8
    +0x28  loadBase             0x4000 in tutti
    +0x2C  version              4

L'immagine caricata e' [module_start, module_start + imageSize) e va a LS
`loadBase`; l'entry sta a `loadBase + entryOffset`.

Il layout non e' dedotto dalla documentazione ma risolto sul binario: nel primo
modulo la tripla PIC

    ila  $r2, 0x41C0
    brsl $r126, <istruzione seguente>
    sf   $r126, $r2, $r126

fissa l'indirizzo di link dell'istruzione dopo il `brsl`. L'unica ipotesi che da'
un risultato tondo -- 0x4000, cioe' esattamente il campo a +0x28 -- e' che
l'immagine parta da module_start e non dopo l'header. Con quella, l'entry cade su
un prologo di funzione pulito (`stqd $r80..$r126`, `rdch $r82, SPU_RdDec`).

Uso:
    python tools/extract_wwsjob_modules.py EBOOT.elf --output spu_jobs/
"""
import argparse
import hashlib
import os
import struct
import sys

MAGIC = 0xC0DEC0DE
HDR_MAGIC_OFF = 0x20          # dove sta il magic dentro l'header
HDR_SIZE = 0x30


def find_modules(blob):
    """Ogni modulo trovato, come dict. Ordinati per offset nel file."""
    out = []
    needle = struct.pack(">I", MAGIC)
    i = blob.find(needle)
    while i != -1:
        st = i - HDR_MAGIC_OFF
        if st >= 0 and st + HDR_SIZE <= len(blob):
            entry, size, bss, unk = struct.unpack_from(">4I", blob, st + 0x10)
            flags, base, ver = struct.unpack_from(">3I", blob, st + 0x24)
            # Un modulo plausibile: entry dentro l'immagine, immagine dentro il
            # file, e le quattro `ila` del bootstrap davvero `ila` (op7 = 0x21).
            ilas_ok = all(
                (struct.unpack_from(">I", blob, st + 4 * k)[0] >> 25) == 0x21
                for k in range(4))
            if (ilas_ok and HDR_SIZE <= size <= 0x40000
                    and st + size <= len(blob) and HDR_SIZE <= entry < size):
                out.append({"offset": st, "entry": entry, "size": size,
                            "bss": bss, "unk": unk, "flags": flags,
                            "base": base, "version": ver})
        i = blob.find(needle, i + 1)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", help="EBOOT.elf (o qualsiasi binario che li contenga)")
    ap.add_argument("--output", default="wwsjob_modules", help="cartella di destinazione")
    ap.add_argument("--elf", action="store_true",
                    help="emette anche un ELF SPU per ogni modulo, pronto per il lifter")
    a = ap.parse_args()

    blob = open(a.binary, "rb").read()
    mods = find_modules(blob)
    if not mods:
        print("nessun modulo WwsJob trovato")
        return

    os.makedirs(a.output, exist_ok=True)
    if a.elf:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from wrap_spu_elf import wrap

    print(f"{len(mods)} moduli WwsJob")
    for m in mods:
        img = blob[m["offset"]:m["offset"] + m["size"]]
        sha = hashlib.sha1(img).hexdigest()[:12]
        name = f"jobmod_{m['offset']:08x}_{sha}"
        with open(os.path.join(a.output, name + ".bin"), "wb") as f:
            f.write(img)
        if a.elf:
            with open(os.path.join(a.output, name + ".elf"), "wb") as f:
                f.write(wrap(img, base=m["base"], entry=m["base"] + m["entry"]))
        print(f"  0x{m['offset']:08X}  size=0x{m['size']:<6X} entry=0x{m['entry']:<6X} "
              f"(LS 0x{m['base'] + m['entry']:X})  bss=0x{m['bss']:<5X} "
              f"base=0x{m['base']:X} flags={m['flags']}  {name}")


if __name__ == "__main__":
    main()
