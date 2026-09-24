#!/bin/bash
# Rigenera tutto il codice SPU liftato del port. Ogni passo qui e' stato pagato
# con un'indagine, e i file che produce non stanno in nessun repo: se si perde
# questo script si perdono i parametri, non solo il tempo di ricostruirli.
set -e
R="$(cd "$(dirname "$0")/.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
PY="$R/.venv/bin/python"
cd "$D"

# 1. I 28 moduli di job WwsJob, che vivono dentro l'EBOOT dietro il magic
#    0xC0DEC0DE. Non sono ELF: header di 0x30 byte, immagine da module_start,
#    base di link 0x4000.
echo "== moduli di job =="
"$PY" "$R/tools/extract_wwsjob_modules.py" EBOOT.elf --output spu_jobs --elf

# 2. Il lift dei moduli + il registry a firma di 16 byte. Gli image id partono
#    da 200 perche' sotto ci sono le immagini a fingerprint e le overlay FMOD.
echo "== lift dei moduli di job =="
"$PY" "$R/tools/lift_jobmods.py" \
    --jobmods spu_jobs --lifted spu_jobs/lifted \
    --register-c gen/jobmods_register.c \
    --register-fn uncharted_jobmods_register_all --image-base 200 \
    --fallthrough-tailcall --merge-blocks

# 3. Le due immagini SPU del titolo (il suo programma e il policy module SPURS)
#    e il registry a fingerprint.
echo "== immagini SPU del titolo =="
"$PY" "$R/tools/build_spu_workloads.py" \
    --images spu_all --lifted spu_lifted \
    --out gen/spu_workloads.c \
    --register-fn spu_register_all --constructor --title uncharted

# 4. Il policy module va rifatto con due ingressi in piu' e tre salti forzati.
#
#    --extra-funcs 0xA2C: e' la `brasl $r79, 0x3FEC0` dentro func_00000A00, che
#    chiama lo stub di salvataggio registri che il modulo si genera da solo in
#    cima alla local store. --auto-functions non puo' vederlo -- ci si arriva
#    solo con un salto calcolato -- e senza questo il modulo cade in BRANCH-TO-0
#    dodici volte per esecuzione.
#
#    --extra-funcs 0x1BDC + --force-indirect 0x2AD4,0x2AF4,0x2B14: 0x1BDC e' il
#    punto in cui l'interprete dei comandi di caricamento legge il comando
#    successivo. I gestori tornano li' cosi': `ila $r6, 0x1BDC ; br 0x2A88`, e
#    il blocco condiviso a 0x2A88 esce con `biz $r4,$r6` / `biz $r75,$r6` /
#    `bi $r6`. Ma r6 e' ANCHE un registro di link (`brsl $r6, 0x2F38`), quindi
#    il lifter classificava quei tre come ritorni di funzione ed emetteva
#    SPU_RET: invece di continuare la lista, il gestore usciva dall'interprete.
#    Misurato: otto comandi in tutta l'esecuzione, tutti il primo della lista di
#    un job -- il codice del job non veniva mai caricato e il manager falliva
#    l'assert del magic 0xC0DEC0DE a 0x33FC.
echo "== policy module, ingressi extra e salti forzati =="
#    --extra-funcs 0x30E4: l'indirizzo di ritorno di `brsl $r6, 0x2F38` (@0x30E0).
#    Il blocco 0x2F38 esce con `bi $r6`, che ora e' un dispatch, e
#    spu_indirect_branch salta solo a ingressi registrati. Misurato: con 0x30E4
#    la pipeline dei job parte in tre corse su tre (14 comandi, un overlay
#    residente); senza, la ricerca fallisce, si finisce nel fallback a microstep
#    e l'interrupt di stall-and-notify non viene piu' preso -- zero comandi.
#
#    NON aggiungere anche 0x1960/0x3030/0x3610 (gli altri ritorni della stessa
#    forma): con quel set l'interrupt sparisce di nuovo, zero comandi su tre
#    corse. Registrare un ingresso spezza la funzione che lo contiene e cambia
#    dei `goto` interni in trasferimenti via trampolino.
#
#    (Il blocco di commenti stava DENTRO il comando e spezzava la
#    continuazione: il policy module veniva liftato senza ingressi extra ne'
#    salti forzati. Sessione 27.)
# (Primo giro su una macchina nuova: il policy module non c'e' ancora, vedi prepare.sh.)
if [ -f spu_all/spurs_policy.elf ]; then
"$PY" "$R/tools/spu_lifter.py" \
    spu_all/spurs_policy.elf --auto-functions spu_all/spurs_policy.elf \
    --symbol-prefix spurs_policy_ -o spu_lifted/spurs_policy \
    --extra-funcs 0xA2C,0x1BDC,0x30E4,0x3528 \
    --force-indirect 0x2AD4,0x2AF4,0x2B14
else echo "manca spu_all/spurs_policy.elf: policy module non liftato (vedi prepare.sh)"; fi
#
#    --extra-funcs 0x3528 (sessione 27): ritorno del `brsl $r0, 0x3600` a
#    0x3524, dentro la finestra di interrupt del PM. Quando l'interrupt di
#    stall-and-notify scatta li', l'`iret` torna a 0x3528 via dispatch, e
#    senza ingresso finiva in BRANCH-TO-0 + microstep ("UNKNOWN word
#    0x7D008102" = ceqhi): il livello dive-boat si fermava dopo il login.

echo "fatto. ora ./build.sh"
