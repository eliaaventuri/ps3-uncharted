#!/usr/bin/env python3
"""Generate a minimal PPC64 big-endian ET_EXEC ELF for boot testing.

The boot path (loader -> lifted code -> guest memory -> run) can only be
exercised end to end with a real image, and the project's own is a decrypted
retail EBOOT that not every checkout has. This emits a few dozen bytes of real
PowerPC that the same pipeline can load, lift, compile and run, so the POSIX
boot has something to boot on a machine with no game on it.

The image mimics what the loader needs from a retail EBOOT: PT_LOAD segments,
an .opd section containing e_entry (PS3 function descriptors are {u32 func,
u32 toc} pairs), and a TOC.

    python tools/make_test_elf.py -o /tmp/hello.elf
"""
import argparse
import struct

TEXT_VA = 0x00010000
OPD_VA  = 0x00020000
TOC_VA  = 0x00030000
SCRATCH = 0x00100000          # inside main memory: the program stores here

# li r3,41 / addi r3,r3,1 / lis r4,0x0010 / stw r3,0(r4) / lwz r5,0(r4)
# / mr r3,r5 / blr        -- leaves 42 in r3 and at guest 0x00100000.
CODE = [0x38600029, 0x38630001, 0x3C800010, 0x90640000, 0x80A40000,
        0x7CA32B78, 0x4E800020]

SHSTR = b"\x00.text\x00.opd\x00.shstrtab\x00"


def build():
    text = b"".join(struct.pack(">I", w) for w in CODE)
    opd  = struct.pack(">II", TEXT_VA, TOC_VA)

    ehsize, phentsize, shentsize = 64, 56, 64
    phnum, shnum = 2, 4

    off_ph   = ehsize
    off_text = off_ph + phnum * phentsize
    off_opd  = off_text + len(text)
    off_str  = off_opd + len(opd)
    off_sh   = off_str + len(SHSTR)

    eh = struct.pack(
        ">16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x02\x01" + b"\x00" * 9,   # ELF64, big-endian
        2, 21, 1,                                # ET_EXEC, EM_PPC64
        OPD_VA,                                  # e_entry: an OPD address
        off_ph, off_sh, 0,
        ehsize, phentsize, phnum, shentsize, shnum, 3)

    ph = (struct.pack(">IIQQQQQQ", 1, 5, off_text, TEXT_VA, TEXT_VA,
                      len(text), len(text), 0x10000) +          # .text  r-x
          struct.pack(">IIQQQQQQ", 1, 6, off_opd, OPD_VA, OPD_VA,
                      len(opd), len(opd), 0x10000))             # .opd   rw-

    sh = (struct.pack(">IIQQQQIIQQ", *([0] * 10)) +
          struct.pack(">IIQQQQIIQQ", SHSTR.index(b".text"), 1, 6,
                      TEXT_VA, off_text, len(text), 0, 0, 4, 0) +
          struct.pack(">IIQQQQIIQQ", SHSTR.index(b".opd"), 1, 3,
                      OPD_VA, off_opd, len(opd), 0, 0, 8, 0) +
          struct.pack(">IIQQQQIIQQ", SHSTR.index(b".shstrtab"), 3, 0,
                      0, off_str, len(SHSTR), 0, 0, 1, 0))

    return eh + ph + text + opd + SHSTR + sh


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-o", "--output", default="hello.elf")
    a = ap.parse_args()
    blob = build()
    with open(a.output, "wb") as f:
        f.write(blob)
    print(f"{a.output}: {len(blob)} bytes")
    print(f"  entry OPD  0x{OPD_VA:08X} -> code 0x{TEXT_VA:08X}, TOC 0x{TOC_VA:08X}")
    print(f"  expects 42 in r3 and at guest 0x{SCRATCH:08X}")
