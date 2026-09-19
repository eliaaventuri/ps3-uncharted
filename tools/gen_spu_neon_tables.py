#!/usr/bin/env python3
"""Genera runtime/spu/spu_neon_tables.h (indici per vqtbl1q_u8 dei byte-shift
SPU e dei generatori cbd/chd/cwd/cdd). Vedi il commento in testa al file."""
import os, sys
W = lambda p: (p & ~3) | (3 - (p & 3))
def fmt(name, rows, comment):
    out = [f"/* {comment} */", f"static const uint8_t {name}[{len(rows)}][16] = {{"]
    for r in rows:
        out.append("    {" + ",".join(f"0x{v:02X}" for v in r) + "},")
    out.append("};")
    return "\n".join(out)
rot = [[W((W(h) + sh) & 15) for h in range(16)] for sh in range(16)]
shl = [[(W(W(h) + sh) if W(h) + sh < 16 else 0xFF) for h in range(16)] for sh in range(17)]
shr = [[(W(W(h) - sh) if W(h) - sh >= 0 else 0xFF) for h in range(16)] for sh in range(17)]
def base():
    r = [0] * 16
    for t in range(16): r[W(t)] = 0x10 + t
    return r
cbd = []; chd = []; cwd = []; cdd = []
for pos in range(16):
    r = base(); r[W(pos & 0xF)] = 0x03; cbd.append(r)
    r = base(); p = (pos & 0xF) & ~1; r[W(p)] = 0x02; r[W(p + 1)] = 0x03; chd.append(r)
    r = base(); p = (pos & 0xF) & ~3
    for k in range(4): r[W(p + k)] = k
    cwd.append(r)
    r = base(); p = (pos & 0xF) & ~7
    for k in range(8): r[W(p + k)] = k
    cdd.append(r)
hdr = """/* Tabelle degli indici per vqtbl1q_u8 (sessione 36). Generate da
 * tools/gen_spu_neon_tables.py -- non modificare a mano.
 *
 * u128 e' "parole in ordine SPU, byte di ogni parola in ordine host (LE)":
 * il byte SPU j sta nel byte host SPU_W(j) = (j & ~3) | (3 - (j & 3)).
 * Ogni riga e' il vettore di indici che porta il byte host h al byte host
 * di destinazione secondo la semantica SPU dell'istruzione; 0xFF = fuori
 * tabella = byte a zero (e' cosi' che tbl riempie). */
#ifndef SPU_NEON_TABLES_H
#define SPU_NEON_TABLES_H
#include <stdint.h>
"""
body = "\n".join([
    fmt("spu__rotqby_tbl", rot, "rotqby sh (0..15): r.spu[i] = a.spu[(i+sh)&15]"),
    fmt("spu__shlqby_tbl", shl, "shlqby sh (0..16): r.spu[i] = i+sh<16 ? a.spu[i+sh] : 0"),
    fmt("spu__rotqmby_tbl", shr, "rotqmby sh (0..16, scorrimento a destra): r.spu[i] = i-sh>=0 ? a.spu[i-sh] : 0"),
    fmt("spu__cbd_tbl", cbd, "cbd pos: controllo di shufb con il byte preferito (SPU byte 3) in posizione pos"),
    fmt("spu__chd_tbl", chd, "chd pos: halfword preferita (SPU byte 2,3) in posizione pos&~1"),
    fmt("spu__cwd_tbl", cwd, "cwd pos: parola preferita (SPU byte 0..3) in posizione pos&~3"),
    fmt("spu__cdd_tbl", cdd, "cdd pos: doppia parola preferita (SPU byte 0..7) in posizione pos&~7"),
])
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "runtime", "spu", "spu_neon_tables.h")
open(out, "w").write(hdr + body + "\n#endif /* SPU_NEON_TABLES_H */\n")
print("scritto", os.path.normpath(out))
