#!/bin/bash
# prepare.sh -- dall'EBOOT.elf decifrato al codice PPU liftato, alla tabella
# NID e alle immagini SPU. Poi: relift_spu.sh, build.sh, run.sh.
# Ogni passo e' stato verificato contro i file di lavoro del 24/09/2026:
# stessi byte, tranne names.json (finisce solo nei commenti del C liftato) e la
# tabella NID, che segue le funzioni HLE presenti oggi nel runtime.
set -e
R="$(cd "$(dirname "$0")/.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
PY="$R/.venv/bin/python"
[ -x "$PY" ] || PY=python3
[ -f "$D/EBOOT.elf" ] || { echo "manca $D/EBOOT.elf (decifrato: rpcs3 --decrypt)"; exit 1; }
mkdir -p "$D/analysis" "$D/gen" "$D/spu_all" "$D/spu_dump"
cd "$D"

echo "== analisi dell'ELF (funzioni, import, layout) =="
"$PY" "$R/tools/ppu_loader.py" EBOOT.elf -o analysis
mv analysis/EBOOT.functions.json analysis/EBOOT.functions.orig.json

# Gli ingressi raggiungibili solo per salto calcolato, raccolti corsa dopo corsa
# da iterate.sh. Il repo ne tiene la lista: senza, il titolo si ferma presto.
[ -f analysis/extra_targets.txt ] || cp "$R/uncharted/extra_targets.txt" analysis/
echo "== ingressi indiretti =="
"$PY" "$R/tools/find_indirect_entries.py" EBOOT.elf \
    --functions analysis/EBOOT.functions.orig.json \
    --extra "$(sed 's/,$//' analysis/extra_targets.txt)" -o analysis/EBOOT.functions.json
"$PY" "$R/tools/names_from_asserts.py" EBOOT.elf \
    --functions analysis/EBOOT.functions.json -o analysis/names.json > /dev/null

echo "== lift PPU (~12 s) =="
rm -rf recomp
"$PY" "$R/tools/ppu_lifter.py" EBOOT.elf \
    --functions analysis/EBOOT.functions.json \
    --hle-stubs analysis/EBOOT.imports.json \
    --names analysis/names.json --toc 0x7d3190 -o recomp

echo "== tabella NID =="
(cd "$R" && "$PY" tools/gen_hle_nids.py --all --out "$D/gen/ppu_hle_nids.cpp")

echo "== immagini SPU =="
"$PY" "$R/tools/extract_spu_images.py" EBOOT.elf -o spu_all
# Il policy module SPURS non sta nell'EBOOT: il titolo lo assembla in memoria.
# Si ottiene con una corsa del port con SPU_PM_DUMP=$D/spu_dump.
PM=$(ls spu_dump/spu_pm_*.bin 2>/dev/null | head -1 || true)
if [ -n "$PM" ]; then
    "$PY" "$R/tools/wrap_spu_elf.py" "$PM" --entry 0xa00 --base 0xa00 --out spu_all/spurs_policy.elf
else
    echo "NOTA: manca il policy module. Primo giro: relift_spu.sh, build.sh, poi"
    echo "  uncharted/runfor.sh 30 pm.log SPU_PM_DUMP=spu_dump"
    echo "e rilancia prepare.sh e relift_spu.sh."
fi
echo "fatto. ora uncharted/relift_spu.sh e uncharted/build.sh"
