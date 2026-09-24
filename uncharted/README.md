# Building Uncharted: Drake's Fortune

These scripts turn **your own copy** of the game into the native macOS port.
No game files are in this repository; everything the scripts produce goes into
a separate working folder outside it.

## What you need

- An Apple Silicon Mac, Xcode Command Line Tools, Homebrew (`cmake`, `ninja`, `sdl2`).
- The European disc, **BCES00065**, as an ISO. Other regions have not been tried.
- The game executable **decrypted**: `rpcs3 --decrypt` on `PS3_GAME/USRDIR/EBOOT.BIN`
  produces the `EBOOT.elf` these scripts expect.

The working folder defaults to `uncharted-data/` **next to** the repository
(`../uncharted-data`); set `UNCHARTED_DATA=/some/path` to put it elsewhere.
Put `EBOOT.elf` there.

## Steps

```bash
# 0. the runtime (once, from the repository root)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt

# 1. mount the disc read-only where run.sh looks for it
hdiutil attach -readonly -nobrowse -noverify -mountpoint /tmp/uc_iso "<your .iso>"

# 2. analyse the executable, translate the PPU code (~12 s), NID table, SPU images
uncharted/prepare.sh

# 3. translate the SPU programs and the 28 job modules hidden in the executable
uncharted/relift_spu.sh

# 4. compile everything and link the port (the first time takes a while)
uncharted/build.sh

# 5. play
uncharted/run.sh
```

**First time only:** the SPURS policy module is not stored in the executable —
the game assembles it in memory at startup. After step 4, run the port once
with `SPU_PM_DUMP` so it writes the module out, then repeat steps 2–4:

```bash
uncharted/runfor.sh 30 pm.log SPU_PM_DUMP=spu_dump   # spu_dump/ inside the working folder
```

## Controls

The keyboard acts as controller 1 while the window has focus: arrows = D-pad,
Space or X = ✕, C = ◯, Enter = START. Without `PAD_SCRIPT`, `run.sh` presses ✕
automatically for the first 28 seconds to get through the start screens
(`LBP_AUTOPRESS=0` turns that off).

## The scripts

| Script | What it does |
|---|---|
| `prepare.sh` | Executable analysis, indirect-call entries, PPU lift, NID table, SPU image extraction |
| `relift_spu.sh` | SPU job modules and policy module, with the extra entries each one needs (explained inline) |
| `build.sh` | Compiles the lifted code and links it with the runtime into `uncharted` |
| `run.sh` | Starts the port with the settings the game needs; every setting is explained inline |
| `runfor.sh` | Runs for N seconds and stops, for measurements |
| `iterate.sh` | Run → collect unresolved indirect calls → re-lift, until nothing new turns up |
| `extra_targets.txt` | Code addresses reachable only through computed jumps, found by `iterate.sh` |

These are the scripts used day to day in development; their comments are in
Italian, like the work diary in [`docs/STATO.md`](../docs/STATO.md). As of
24/09/2026 they have not yet been run end to end on a clean machine: if a step
fails, please open an issue.
