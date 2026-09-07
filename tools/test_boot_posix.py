#!/usr/bin/env python3
"""End-to-end boot test: synthetic ELF -> loader -> lifter -> compile -> run.

The project's boot harness (runtime/ppu/tests/boot_main.cpp) needs a decrypted
retail EBOOT, so on a checkout without one nothing exercised the boot path at
all -- and on POSIX that path had never been *run*, only compiled. This builds
a tiny PPC64 image with tools/make_test_elf.py and drives it through the real
boot sequence (ppu_load_elf, ppu_recomp_register, the HLE/syscall layers,
ppu_run), then checks the guest executed: 42 at guest 0x00100000.

Needs build/libps3recomp_runtime.a. Skips without it, unless CI=1.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY = sys.executable

INCLUDES = ["include", "runtime/ppu", "runtime/spu", "libs/system", "libs/video",
            "libs/filesystem", "libs/input", "libs/audio", "libs/network",
            "libs/spurs", "libs/sync", "libs/codec", "libs/font", "libs/misc"]

SOURCES = ["runtime/ppu/tests/boot_smoke/main.cpp", "runtime/ppu/ppu_loader.cpp",
           "runtime/ppu/ppu_hle.cpp", "runtime/ppu/ppu_fs.cpp",
           "runtime/ppu/ppu_sysprx.cpp"]


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def main():
    lib = os.path.join(ROOT, "build", "libps3recomp_runtime.a")
    if not os.path.exists(lib):
        msg = "build/libps3recomp_runtime.a not found -- build the runtime first"
        if os.environ.get("CI"):
            print("FAIL:", msg)
            return 1
        print("SKIP:", msg)
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        elf = os.path.join(tmp, "hello.elf")
        r = run([PY, "tools/make_test_elf.py", "-o", elf])
        if r.returncode:
            print("FAIL: make_test_elf.py\n" + r.stderr)
            return 1

        r = run([PY, "tools/ppu_loader.py", elf, "-o", tmp])
        if r.returncode:
            print("FAIL: ppu_loader.py\n" + r.stderr)
            return 1

        recomp = os.path.join(tmp, "recomp")
        r = run([PY, "tools/ppu_lifter.py", elf, "--functions",
                 os.path.join(tmp, "hello.functions.json"),
                 "--toc", "0x30000", "-o", recomp])
        if r.returncode:
            print("FAIL: ppu_lifter.py\n" + r.stderr)
            return 1

        out = os.path.join(tmp, "boot_smoke")
        cmd = [os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-o", out]
        if sys.platform == "darwin":
            # ppu_hle.cpp references the flOw port's weak SPURS hook; Mach-O
            # needs the undefined symbol declared at link time, ELF does not.
            cmd += ["-Wl,-U,_flow_spurs_kernel_spawn_postinit"]
        for inc in INCLUDES:
            cmd += ["-I", os.path.join(ROOT, inc)]
        cmd += ["-I", recomp, "-isystem", "/opt/homebrew/include"]
        cmd += [os.path.join(ROOT, s) for s in SOURCES]
        cmd += [os.path.join(recomp, f) for f in sorted(os.listdir(recomp))
                if f.endswith(".cpp")]
        cmd += [lib]
        r = run(cmd)
        if r.returncode:
            print("FAIL: compile\n" + r.stderr[-2000:])
            return 1

        r = run([out, elf])
        print(r.stdout.strip())
        if r.returncode:
            print("FAIL: the guest did not execute correctly")
            return 1

    print("\nPOSIX boot test: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
