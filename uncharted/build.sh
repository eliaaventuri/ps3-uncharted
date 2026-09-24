#!/bin/bash
# Costruisce il port di Uncharted: codice liftato + runtime PPU + harness di boot.
# I dati del gioco vivono qui, fuori dal repo; il repo resta pulito.
set -e
export PATH="/opt/homebrew/bin:$PATH"
R="$(cd "$(dirname "$0")/.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
mkdir -p "$D/obj"
# via gli oggetti di chunk spariti dopo un re-lift con un numero diverso di chunk
for o in "$D/obj"/ppu_recomp_*.o; do
    [ -f "$o" ] || continue
    n=$(basename "$o" .o); n=${n##*_}
    [ -f "$D/recomp/ppu_recomp_$n.cpp" ] || rm -f "$o"
done

INC=(-I "$R/include" -I "$D/recomp" -I "$R/runtime/ppu" -I "$R/libs/system"
     -I "$R/libs/video" -I "$R/libs/filesystem" -I "$R/libs/input" -I "$R/libs/audio"
     -I "$R/libs/network" -I "$R/libs/spurs" -I "$R/libs/sync" -I "$R/libs/codec"
     -I "$R/libs/font" -I "$R/libs/misc" -I "$R/runtime/spu"
     -isystem /opt/homebrew/include)

cd "$R"
# I chunk liftati: ricompilati solo quando il .cpp e' piu' recente del .o
# (sono ~600k righe l'uno, qualche minuto a testa).
for src in "$D/recomp"/ppu_recomp_*.cpp; do
    [ -f "$src" ] || continue
    i=$(basename "$src" .cpp); i=${i##*_}; obj="$D/obj/ppu_recomp_$i.o"
    [ -f "$src" ] || continue
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  cc ppu_recomp_$i (lungo)"
        # -O1: a -O0 il codice liftato e' cosi' lento che l'inizializzazione del
        # gioco sembra un deadlock. -O2 non vale il tempo di compilazione qui.
        clang++ -std=c++20 ${PPU_OPT:--O1} -w "${INC[@]}" -c "$src" -o "$obj" &
    fi
done
wait

# Il codice SPU liftato del titolo, i 28 moduli di job WwsJob estratti
# dall'EBOOT, e i due registry (fingerprint per le immagini, firma di 16 byte
# per gli overlay dei job).
# SPU_DEFS: define extra per il codice SPU. Default -DSPU_LS_NODIAG (s.36:
# senza il ramo delle sonde negli accessi alla LS il compilatore tiene i
# registri guest nei registri host: 9,4 -> 10,3 fps). Per le sonde
# SPU_LS_* (watch, LOWREAD, ...): SPU_DEFS=" " ./build.sh. Se le define
# cambiano rispetto all'ultima build, gli oggetti SPU vengono rifatti tutti.
SPU_DEFS=${SPU_DEFS--DSPU_LS_NODIAG}
if [ "$(cat "$D/obj/spu_defs.stamp" 2>/dev/null)" != "$SPU_DEFS" ]; then
    rm -f "$D/obj"/spu_*.o
    echo "$SPU_DEFS" > "$D/obj/spu_defs.stamp"
fi
for src in "$D/spu_lifted"/*/spu_recomp.c "$D/spu_jobs/lifted"/*/spu_recomp.c \
           "$D/gen/spu_workloads.c" "$D/gen/jobmods_register.c"; do
    [ -f "$src" ] || continue
    n="spu_$(basename "$(dirname "$src")")_$(basename "$src" .c)"
    obj="$D/obj/$n.o"
    # anche quando un header del runtime SPU e' cambiato: il codice liftato
    # e' compilato con gli offset di spu_context e i corpi delle macro di
    # dispatch (sessione 28: campo aggiunto -> "Illegal instruction" all'avvio)
    # (`|| true`: con set -e un .o mancante faceva uscire lo script in silenzio, s.31)
    hdr_new=$( { [ -f "$obj" ] && find "$R/runtime/spu" -name '*.h' -newer "$obj" | head -1; } || true )
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "$hdr_new" ]; then
        echo "  cc $n"
        clang -std=gnu17 ${SPU_OPT:--O2} ${SPU_DEFS} -w "${INC[@]}" -I "$R/runtime/spu" -c "$src" -o "$obj" &
    fi
done
wait

# La tabella NID e' per-port: ppu_hle_register_all() nel runtime e' weak e vuota,
# e senza la versione generata ogni import firmware resta non risolto.
echo "  cc ppu_hle_nids (generata)"
clang++ -std=c++20 -O1 -w "${INC[@]}" -c "$D/gen/ppu_hle_nids.cpp" -o "$D/obj/ppu_hle_nids.o"

for f in runtime/ppu/ppu_loader.cpp runtime/ppu/ppu_hle.cpp runtime/ppu/ppu_fs.cpp \
         runtime/ppu/ppu_sysprx.cpp runtime/ppu/tests/boot_main.cpp; do
    n=$(basename "$f" .cpp)
    echo "  cc $n"
    clang++ -std=c++20 -O1 -w "${INC[@]}" -c "$f" -o "$D/obj/$n.o" &
done
wait

echo "  link"
clang++ ${SPU_LDFLAGS:-} -o "$D/uncharted" -Wl,-U,_flow_spurs_kernel_spawn_postinit -Wl,-U,_spu_register_all \
    "$D/obj/"*.o "$R/${RT_BUILD:-build}/libps3recomp_runtime.a" \
    -L/opt/homebrew/lib -lSDL2 \
    -framework Metal -framework QuartzCore -framework Foundation -framework AppKit
echo "fatto: $D/uncharted"
