#!/usr/bin/env python3
"""Find alternate function entry points that only a runtime pointer reaches.

The lifter's mid-function pass generates a tail-entry wrapper for every address
some instruction BRANCHES to. That covers everything visible statically, and
misses the case where the address lives in DATA and is only ever loaded into a
register and called through: nothing in the code stream mentions it, so the
lifter has no reason to emit an entry for it, and at runtime the dispatch
reports "unresolved indirect call".

Uncharted hits this during renderer setup. Its compiler emits alternate entry
points as a table of

    li  r11, <n>
    b   <shared body>

pairs in the tail of the owning function; a function pointer taken to one of
those pairs enters the body with r11 preset. There are 302 of them in
BCES00065, and one is what the primary-render-target path calls -- the call
went nowhere, the return value was whatever the register held, and the title
carried on with a garbage pointer.

Merges the addresses it finds into a functions.json so the lifter emits each as
its own function:

    python tools/find_indirect_entries.py EBOOT.elf \
        --functions analysis/EBOOT.functions.json -o analysis/EBOOT.functions.json
"""
import argparse
import json
import struct


def segments(elf_bytes):
    """Program headers of a PPC64 big-endian ELF: (vaddr, offset, filesz, flags)."""
    e_phoff = struct.unpack_from(">Q", elf_bytes, 0x20)[0]
    e_phentsize = struct.unpack_from(">H", elf_bytes, 0x36)[0]
    e_phnum = struct.unpack_from(">H", elf_bytes, 0x38)[0]
    out = []
    for i in range(e_phnum):
        base = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from(">II", elf_bytes, base)
        p_offset, p_vaddr = struct.unpack_from(">QQ", elf_bytes, base + 8)
        p_filesz = struct.unpack_from(">Q", elf_bytes, base + 32)[0]
        if p_type == 1:                       # PT_LOAD
            out.append((p_vaddr, p_offset, p_filesz, p_flags))
    return out


def _is_terminator(w):
    """Unconditional branch (b/ba), blr or bctr -- nothing falls through these."""
    op = w >> 26
    if op == 18:                              # b / ba / bl / bla
        return (w & 1) == 0                   # not a call
    if op == 19:                              # bclr / bcctr family
        xo = (w >> 1) & 0x3FF
        return xo in (16, 528) and ((w >> 21) & 0x1F) == 20   # unconditional
    return False


def code_words(elf_bytes, lo, hi):
    """Every 4-byte-aligned word in the image whose value is an address inside
    [lo, hi) -- i.e. the pool a jump table's entries come from."""
    out = set()
    for _, offset, filesz, _ in segments(elf_bytes):
        for off in range(0, max(0, filesz - 3), 4):
            v = struct.unpack_from(">I", elf_bytes, offset + off)[0]
            if lo <= v < hi and (v & 3) == 0:
                out.add(v)
    return out


def find_table_targets(elf_bytes, known):
    """Switch-case targets: an address that some DATA word holds and that sits
    right after a terminator, so nothing falls into it.

    The `li r11, N ; b` slots are one shape of alternate entry; a compiler's
    jump table is the other, and its case bodies carry no marker at all.
    Requiring BOTH "a data word points here" and "the previous instruction
    cannot fall through" is what keeps this from flooding the function list.

    Uncharted needs it for PackageMgr::ProtectedTick, whose level-loading
    switch dispatches through such a table.
    """
    segs = segments(elf_bytes)
    ex = [(v, o, sz) for v, o, sz, fl in segs if fl & 1]
    if not ex:
        return set()
    lo = min(v for v, _, _ in ex)
    hi = max(v + sz for v, _, sz in ex)
    pool = code_words(elf_bytes, lo, hi)

    found = set()
    for vaddr, offset, filesz in ex:
        for off in range(4, max(0, filesz - 4), 4):
            va = vaddr + off
            if va not in pool or va in known:
                continue
            prev = struct.unpack_from(">I", elf_bytes, offset + off - 4)[0]
            if _is_terminator(prev):
                found.add(va)
    return found


def branch_targets(elf_bytes):
    """Every address some branch instruction names directly."""
    out = set()
    for vaddr, offset, filesz, flags in segments(elf_bytes):
        if not (flags & 1):
            continue
        for off in range(0, max(0, filesz - 3), 4):
            w = struct.unpack_from(">I", elf_bytes, offset + off)[0]
            op = w >> 26
            if op == 18:                      # b / bl / ba / bla
                d = w & 0x03FFFFFC
                if d & 0x02000000:
                    d -= 0x04000000
                out.add(d if (w & 2) else vaddr + off + d)
            elif op == 16:                    # bc
                d = w & 0xFFFC
                if d & 0x8000:
                    d -= 0x10000
                out.add(d if (w & 2) else vaddr + off + d)
    return out


def find_unreachable_blocks(elf_bytes, known, bounds=None):
    """Blocks that nothing can reach except a computed jump.

    A block that (a) follows a terminator, so nothing falls into it, and
    (b) is named by no branch anywhere in the image, is reachable only through
    a register -- a switch table, a function pointer, a continuation. Those are
    exactly the entries the lifter cannot infer, and the ones whose absence
    shows up at runtime as "unresolved indirect call".

    Uncharted's level loader needs this: PackageMgr::ProtectedTick dispatches
    into 0x000E2CF0, which no data word holds (the table stores computed
    offsets) and no branch names.
    """
    named = branch_targets(elf_bytes)
    import bisect as _b
    found = set()
    for vaddr, offset, filesz, flags in segments(elf_bytes):
        if not (flags & 1):
            continue
        for off in range(4, max(0, filesz - 4), 4):
            va = vaddr + off
            if va in known or va in named:
                continue
            prev = struct.unpack_from(">I", elf_bytes, offset + off - 4)[0]
            if not _is_terminator(prev):
                continue
            w = struct.unpack_from(">I", elf_bytes, offset + off)[0]
            if w == 0 or w == 0x60000000:     # padding / nop: not a block start
                continue
            # Only inside a real function. Outside one, "code after a
            # terminator that nothing branches to" is usually data that
            # happens to decode, and there are thousands of those.
            if bounds is not None:
                i = _b.bisect_right(bounds, va) - 1
                if i < 0 or va >= bounds[i] + 0x8000:
                    continue
            found.add(va)
    return found


def find_entries(elf_bytes, known, max_prologue=8):
    """Start addresses of alternate-entry table slots.

    A slot ends in `li r11, N ; b <body>`, but it can begin several
    instructions earlier: Uncharted has slots that preload r8/r6 with
    constants before the discriminant. A function pointer may point at either
    the slot's first instruction or the `li`, so both are emitted.

    The slot's start is found by walking back from the `li` until the previous
    instruction is a terminator -- i.e. to the top of the straight-line run
    that nothing falls into.
    """
    found = set()
    for vaddr, offset, filesz, flags in segments(elf_bytes):
        if not (flags & 1):                   # executable segments only
            continue
        for off in range(0, max(0, filesz - 8), 4):
            w = struct.unpack_from(">I", elf_bytes, offset + off)[0]
            if (w >> 16) != 0x3960:           # li r11, imm
                continue
            w2 = struct.unpack_from(">I", elf_bytes, offset + off + 4)[0]
            if (w2 >> 26) != 18:              # unconditional branch
                continue
            found.add(vaddr + off)

            back = off
            for _ in range(max_prologue):
                if back < 4:
                    break
                prev = struct.unpack_from(">I", elf_bytes, offset + back - 4)[0]
                if _is_terminator(prev):
                    break
                back -= 4
            if back != off:
                found.add(vaddr + back)
    return sorted(va for va in found if va not in known)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--functions", required=True, help="functions.json from ppu_loader.py")
    ap.add_argument("-o", "--output", help="where to write the merged list (default: stdout summary only)")
    ap.add_argument("--computed-blocks", action="store_true",
                    help="also every block nothing branches to (thorough, but each "
                         "entry duplicates code to its function's end: ~9700 on "
                         "BCES00065, which multiplies the lifted output)")
    ap.add_argument("--extra", default="",
                    help="comma-separated addresses to add, e.g. the target a run "
                         "reported as 'unresolved indirect call' -- the cheap way "
                         "to cover a computed entry without the full sweep")
    a = ap.parse_args()

    blob = open(a.elf, "rb").read()
    with open(a.functions) as f:
        doc = json.load(f)
    items = doc if isinstance(doc, list) else doc.get("functions", [])
    known = {int(str(e["start"]), 0) for e in items}

    slots = set(find_entries(blob, known))
    tables = find_table_targets(blob, known) - slots
    extra = {int(x, 0) for x in a.extra.split(",") if x.strip()}
    blocks = set()
    if a.computed_blocks:
        blocks = find_unreachable_blocks(blob, known, sorted(known)) - slots - tables
    found = sorted((slots | tables | blocks | extra) - known)
    print(f"{len(items)} known functions, {len(found)} alternate entries found "
          f"({len(slots)} li-r11 slots, {len(tables)} jump-table targets"
          + (f", {len(blocks)} computed-only blocks" if blocks else "")
          + (f", {len(extra)} explicit" if extra else "") + ")")
    if not found:
        return
    print(f"  first 0x{found[0]:08X}  last 0x{found[-1]:08X}")

    if not a.output:
        return

    # Each entry runs to the end of the function that owns it: the slot falls
    # through into a shared body further down. An empty [start, start) range
    # makes the lifter emit a function whose whole content is
    #   g_trampoline_fn = itself; return;
    # -- a perfect infinite loop, which is what Uncharted's renderer init did
    # until this was fixed.
    import bisect as _bisect
    bounds = sorted(known)
    merged = list(items)
    for va in found:
        i = _bisect.bisect_right(bounds, va)
        end = bounds[i] if i < len(bounds) else va + 0x400
        merged.append({"start": hex(va), "end": hex(end)})
    merged.sort(key=lambda e: int(str(e["start"]), 0))
    out = merged if isinstance(doc, list) else {**doc, "functions": merged}
    with open(a.output, "w") as f:
        json.dump(out, f, indent=1)
    print(f"  wrote {len(merged)} entries -> {a.output}")


if __name__ == "__main__":
    main()
