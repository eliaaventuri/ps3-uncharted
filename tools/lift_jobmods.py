#!/usr/bin/env python3
"""
Batch-lift the extracted WWS SPU job modules to C.

Each module is DMA'd into the SPU job code buffer at LS 0x4000 and entered at
0x4000 + entryOffset. Because only one module is resident in the code buffer at
a time (the FMOD-overlay model), every module lifts at the SAME base (0x4000);
the runtime picks the resident one by content signature.

For each module:
  1. wrap the raw bytes as a minimal SPU ELF (base 0x4000, entry 0x4000+entryOff)
  2. run spu_lifter --auto-functions, which uses find_spu_functions.detect_functions
     to seed from entry + branch/call/jump-table targets -- so rodata past the
     last function is never disassembled (no .word data-as-code).
  3. unique --symbol-prefix per module so all 89 link together.

Output: lbp_spu/lifted/<modname>/spu_recomp.{c,h}
Also writes lbp_spu/lifted/jobmods_manifest.json: [{name, prefix, base, entry,
sig(hex16), size, funcs, unsupported}].
"""
import argparse, glob, hashlib, json, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from wrap_spu_elf import wrap

ROOT   = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
JOBMODS= os.path.join(ROOT, "lbp_spu", "jobmods")
OUTDIR = os.path.join(ROOT, "lbp_spu", "lifted")
LIFTER = os.path.join(os.path.dirname(__file__), "spu_lifter.py")
BASE   = 0x4000

# Per-module code/data boundary. A few images embed a large rodata/const tail
# inside the executable segment; its bytes decode as in-range branches that
# seed spurious "functions", disassembling the data as code (garbage + `.word`
# for undecodable words). The boundary was found by disassembly: after the
# last real unconditional `br`, a zlib-inflate error-string table + float
# const tables run to the image end. Passed to spu_lifter.py --code-end so the
# tail is left un-disassembled. Without it, these two physics jobmods each lift
# ~97 `.word` unsupported (was 0 in the original, pre-git-clean lift).
CODE_END = {
    "jobmod_ef33c6c99dc7_e0A40": 0xAE68,   # real code ends at br 0xA554 @ 0xAE60
    "jobmod_6943e26934ea_e0A20": 0xAE48,   # real code ends at br 0xA534 @ 0xAE40
}
# Ingressi che --auto-functions non vede (raggiunti solo da `bisl $r0, $rN`
# calcolati), passati come --extra-funcs. Senza, il salto finisce nel
# microstep, che non sa eseguire il corpo, e il job si ferma.
EXTRA_FUNCS = {
    # Uncharted, ovl 216: `bisl $r0,$r20` a LS 0x59CC -> 0x6C80 (hbrr + cwd + lqd...),
    # "BRANCH-TO-0 unresolved pc=0x06C80", e la batch job list non finiva mai (sessione 28)
    "jobmod_008eea40_867403af57c7": [0x6C80],
}

def main():
    # I percorsi LBP restano i default: e' il titolo su cui questo e' nato. Ma i
    # moduli WwsJob non sono una cosa di LBP -- Uncharted ne ha 28 dentro
    # l'EBOOT -- e cablare le cartelle rendeva il tool inutilizzabile altrove.
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobmods", default=JOBMODS, help="cartella con i moduli .bin")
    ap.add_argument("--lifted", default=OUTDIR, help="cartella di destinazione del C liftato")
    ap.add_argument("--register-c", default=REGISTER_C, help="file di registrazione da generare")
    ap.add_argument("--register-fn", default="lbp_jobmods_register_all",
                    help="nome della funzione di registrazione")
    ap.add_argument("--image-base", type=int, default=JOBMOD_IMAGE_BASE,
                    help="primo image id da assegnare")
    a = ap.parse_args()

    mods = sorted(glob.glob(os.path.join(a.jobmods, "*.bin")))
    outdir = a.lifted
    os.makedirs(outdir, exist_ok=True)
    manifest = []
    tmp_elf = os.path.join(outdir, "_tmp.elf")
    for path in mods:
        raw = open(path, "rb").read()
        name = os.path.splitext(os.path.basename(path))[0]         # jobmod_<sha>_e<entry>
        sha  = name.split("_")[1]
        entry_off = struct.unpack_from(">I", raw, 0x10)[0]
        entry = BASE + entry_off
        prefix = f"{name}_"
        outsub = os.path.join(outdir, name)
        # content signature: first 16 bytes (the 4 ila header uniquely identify a module)
        sig = raw[:16]

        elf = wrap(raw, base=BASE, entry=entry)
        with open(tmp_elf, "wb") as f:
            f.write(elf)

        cmd = [sys.executable, LIFTER, "--auto-functions", tmp_elf, "--base", hex(BASE),
               "--symbol-prefix", prefix, "-o", outsub,
               "--source-name", "spu_recomp.c", "--header-name", "spu_recomp.h"]
        if name in CODE_END:
            cmd += ["--code-end", hex(CODE_END[name])]
        # Tabelle di puntatori a funzione (sessione 28): i moduli WwsJob di
        # Uncharted chiamano i gestori con `bisl $r0, $rN` da tabelle di parole
        # (es. 216: LS 0x13FB0.., 0x14AE0..), che --auto-functions non vede.
        # Ogni corsa di >= 6 parole allineate che cadono nel modulo e' una
        # tabella: i suoi elementi diventano ingressi extra.
        extras = set(EXTRA_FUNCS.get(name, []))
        cands = []
        for off in range(0, len(raw) - 4, 4):
            w = struct.unpack_from(">I", raw, off)[0]
            cands.append(w if (BASE <= w < BASE + len(raw) and w % 4 == 0 and w != BASE) else None)
        # una corsa puo' contenere fino a 3 zeri di fila (tabelle sparse:
        # 216 ha {0,0,0,6C80,A020,0,0,0} a LS 0x14060); vale con >= 2 voci
        # se contiene zeri, >= 6 se piena
        zeros = [struct.unpack_from(">I", raw, off)[0] == 0 for off in range(0, len(raw) - 4, 4)]
        run = []; nz = 0; zrun = 0
        for w, z in list(zip(cands, zeros)) + [(None, False)]:
            if w is not None: run.append(w); zrun = 0
            elif z and zrun < 3 and run: zrun += 1; nz += 1
            else:
                if len(run) >= 6 or (len(run) >= 2 and nz): extras.update(run)
                run = []; nz = 0; zrun = 0
        if extras:
            cmd += ["--extra-funcs", ",".join(hex(x) for x in sorted(extras))]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  FAIL {name}\n{r.stderr}", file=sys.stderr)
            continue
        # parse stats from lifter stdout
        funcs = unsup = 0
        for line in r.stdout.splitlines():
            s = line.strip()
            if s.endswith("function(s) lifted"):
                funcs = int(s.split()[0])
            if "unsupported instruction(s)" in s:
                unsup = int(s.split()[0])
        manifest.append({
            "name": name, "prefix": prefix, "base": BASE, "entry": entry,
            "entry_off": entry_off, "sig": sig.hex(), "size": len(raw),
            "funcs": funcs, "unsupported": unsup,
        })
        flag = "  <-- UNSUPPORTED" if unsup else ""
        print(f"  {name:40} funcs={funcs:3} unsup={unsup:3} entry=0x{entry:X}{flag}")

    if os.path.exists(tmp_elf):
        os.remove(tmp_elf)
    with open(os.path.join(outdir, "jobmods_manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    emit_register(manifest, a.register_c, a.register_fn, a.image_base)
    tot_unsup = sum(m["unsupported"] for m in manifest)
    print(f"\nLifted {len(manifest)} module(s), {tot_unsup} total unsupported instruction(s)")
    print(f"Manifest: {os.path.join(outdir, 'jobmods_manifest.json')}")

# Image ids for the job-code overlays. Well clear of the fingerprint-dispatched
# images (1..25), the FMOD overlays (60..65) and the taskset policy (100).
JOBMOD_IMAGE_BASE = 200
REGISTER_C = os.path.join(ROOT, "lbp", "gen", "jobmods_register.c")

def emit_register(manifest, register_c=None, register_fn="lbp_jobmods_register_all",
                  image_base=None):
    """Generate lbp/gen/jobmods_register.c: register every job module's lifted
    functions under a distinct image id, plus its 16-byte content signature so
    spu_overlay_note_get flips resident_ovl when the module DMAs into the job
    code buffer (LS 0x4000). Dispatched exactly like the FMOD codec overlays."""
    if register_c is None:
        register_c = REGISTER_C
    if image_base is None:
        image_base = JOBMOD_IMAGE_BASE
    L = []
    L.append("/* jobmods_register.c -- GENERATED by tools/lift_jobmods.py.")
    L.append(" * WWS SPU job-code modules extracted from patch.sdat/data.sdat. Every module")
    L.append(" * streams into the same job code buffer (LS 0x4000) and is entered at")
    L.append(" * 0x4000+entryOffset; only one is resident at a time, so each registers its")
    L.append(" * lifted functions under a distinct image id and a 16-byte content signature.")
    L.append(" * spu_overlay_note_get() sets spu_context.resident_ovl on the DMA that copies")
    L.append(" * the module in; dispatch then resolves 0x4000+ pcs against that overlay. */")
    L.append("#include <stdint.h>")
    L.append("")
    L.append("extern void spu_begin_image(int image_id);")
    L.append("extern void spu_overlay_register_sig(const uint8_t sig[16], int image_id);")
    L.append("")
    for m in manifest:
        L.append(f"extern void {m['prefix']}spu_recomp_register(void);")
    L.append("")
    L.append(f"void {register_fn}(void)")
    L.append("{")
    for i, m in enumerate(manifest):
        img = image_base + i
        sig = bytes.fromhex(m["sig"])
        arr = ",".join(f"0x{b:02X}" for b in sig)
        L.append(f"    {{ static const uint8_t sig[16] = {{{arr}}};")
        L.append(f"      spu_begin_image({img}); {m['prefix']}spu_recomp_register();")
        L.append(f"      spu_overlay_register_sig(sig, {img}); }}   /* {m['name']} entry 0x{m['entry']:X} */")
    L.append("    spu_begin_image(0);")
    L.append("}")
    L.append("")
    L.append("__attribute__((constructor)) static void jobmods_register_ctor(void)")
    L.append("{")
    L.append(f"    {register_fn}();")
    L.append("}")
    L.append("")
    os.makedirs(os.path.dirname(os.path.abspath(register_c)), exist_ok=True)
    with open(register_c, "w") as f:
        f.write("\n".join(L))
    print(f"Wrote {register_c} ({len(manifest)} modules, image ids "
          f"{image_base}..{image_base + len(manifest) - 1})")

if __name__ == "__main__":
    main()
