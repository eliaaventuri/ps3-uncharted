#!/usr/bin/env python3
"""Recover function names from a stripped retail EBOOT's assert strings.

A retail PS3 build has no .symtab, so every function is func_XXXXXXXX and a
backtrace says nothing. But titles built with asserts keep the strings the
assert macro passes -- the source file and __PRETTY_FUNCTION__, which carries
the full C++ signature -- and those are loaded from the TOC inside the very
function they name.

Uncharted does it in two steps, which is why a single-level TOC scan finds
nothing:

    lwz  r30, -0x793C(r2)      ; a base pointer out of the TOC
    ...
    lwz  r0,  -0x7F64(r30)     ; the actual string pointer, off that base

So this walks each function, tracks what every register holds after a load
from r2 (and after a load off such a register), and resolves any value that
points at a plausible C++ signature.

Emits the JSON map ppu_lifter.py takes via --names, so the lifted C carries the
real names as comments, and prints them for eyeballing.

    python tools/names_from_asserts.py EBOOT.elf \
        --functions analysis/EBOOT.functions.json -o analysis/names.json
"""
import argparse
import json
import re
import struct

SIG = re.compile(r"^[A-Za-z_][\w:<>,~\s\*&\[\]]*\(")   # "void Foo::Bar(" etc.


def segments(b):
    e_phoff = struct.unpack_from(">Q", b, 0x20)[0]
    e_phentsize = struct.unpack_from(">H", b, 0x36)[0]
    e_phnum = struct.unpack_from(">H", b, 0x38)[0]
    out = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        if struct.unpack_from(">I", b, o)[0] != 1:
            continue
        p_offset, p_vaddr = struct.unpack_from(">QQ", b, o + 8)
        p_filesz = struct.unpack_from(">Q", b, o + 32)[0]
        out.append((p_vaddr, p_offset, p_filesz))
    return out


class Image:
    def __init__(self, blob):
        self.b = blob
        self.segs = segments(blob)

    def read(self, va, n):
        for v, off, sz in self.segs:
            if v <= va < v + sz:
                return self.b[off + (va - v): off + (va - v) + n]
        return None

    def u32(self, va):
        d = self.read(va, 4)
        return struct.unpack(">I", d)[0] if d else None

    def cstr(self, va, limit=400):
        d = self.read(va, limit)
        if not d:
            return None
        z = d.find(b"\x00")
        if z <= 0:
            return None
        try:
            s = d[:z].decode("ascii")
        except UnicodeDecodeError:
            return None
        return s if s.isprintable() else None


def simm(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def names_in(img, toc, start, end):
    """Signatures a function's own code loads, via r2 or a base register."""
    base = {2: toc}          # register -> value it holds (TOC-derived only)
    found = []
    for va in range(start, end, 4):
        w = img.u32(va)
        if w is None:
            break
        if (w >> 26) != 32:              # lwz only
            continue
        rt, ra = (w >> 21) & 0x1F, (w >> 16) & 0x1F
        if ra not in base:
            continue
        addr = base[ra] + simm(w)
        val = img.u32(addr)
        if val is None:
            continue
        base[rt] = val                   # may itself be a base pointer
        s = img.cstr(val)
        if s and len(s) > 8 and SIG.match(s):
            if s not in found:
                found.append(s)
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--functions", required=True)
    ap.add_argument("--toc", default="0x7d3190")
    ap.add_argument("-o", "--output")
    ap.add_argument("--max-span", type=lambda s: int(s, 0), default=0x8000,
                    help="skip functions larger than this (default 32 KB)")
    a = ap.parse_args()

    img = Image(open(a.elf, "rb").read())
    toc = int(a.toc, 0)
    doc = json.load(open(a.functions))
    items = doc if isinstance(doc, list) else doc.get("functions", [])
    starts = sorted({int(str(e["start"]), 0) for e in items})

    out, named = {}, 0
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else s + 0x400
        if e - s > a.max_span:
            e = s + a.max_span
        got = names_in(img, toc, s, e)
        if got:
            named += 1
            out[hex(s)] = {"label": got[0], "all": got}

    print(f"{len(starts)} functions, {named} carry an assert signature")
    for k in list(out)[:8]:
        print(f"  {k}  {out[k]['label']}")
    if a.output:
        json.dump(out, open(a.output, "w"), indent=1)
        print(f"  wrote {a.output}")


if __name__ == "__main__":
    main()
