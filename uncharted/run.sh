#!/bin/bash
# Avvia il port con i parametri che Uncharted richiede. Ognuno è stato pagato
# con una sessione di indagine, quindi vivono qui e non nella memoria di nessuno.
R="$(cd "$(dirname "$0")/.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"

# La finestra HLE va fuori dalla memoria del titolo (IO a 0x21000000, heap fino
# a ~0x2B200000) e fuori dalla memoria locale RSX (0xC0000000).
export PS3_HLE_INJECT_BASE=0x50000000

# SPU_TAILRET e' SPENTO: la sua euristica (un trasferimento il cui target
# coincide con gpr[0] E' un ritorno) inghiotte anche i salti CALCOLATI verso
# r0 -- fra cui il `bid $r0` che chiude la finestra di interrupt del policy
# module, cioe' l'unico punto in cui l'interrupt di stall-and-notify puo'
# essere preso. Misurato: con TAILRET acceso 14 comandi di job per corsa e un
# solo overlay residente; spento (e con SPU_RETLINK, che fa lo stesso lavoro in
# modo esatto) oltre 400 comandi, due overlay, e il ciclo Load->Run->Store che
# si chiude e riparte. Il motivo per cui era acceso -- la crescita dello stack
# host sui ritorni via registro diverso da r0 -- lo copre SPU_RETLINK.
export SPU_TAILRET=${SPU_TAILRET:-0}

# Un `bi $rN` con rN != r0 puo' essere il ritorno del brsl che sta girando: il
# lifter ne emette un dispatch, e senza questo il blocco di ritorno viene
# eseguito DUE volte (una nel drain della chiamata, una per la caduta in avanti
# del chiamante). Il confronto e' con l'indirizzo di ritorno vero, non con
# gpr[0]: e' la versione esatta di cio' che SPU_TAILRET indovinava.
export SPU_RETLINK=${SPU_RETLINK:-1}

# /dev_hdd1 e' il disco scratch della PS3: FIOS ci tiene la cache e deve poterci
# scrivere. Senza questo cade sull'ISO, in sola lettura, e il titolo prosegue
# usando il codice d'errore come puntatore.
export PS3_HDD1_ROOT="$D/hdd1"
mkdir -p "$PS3_HDD1_ROOT"

# /dev_hdd0 e' il disco di sistema: salvataggi e dati installati.
export PS3_HDD0_ROOT="$D/hdd0"
mkdir -p "$PS3_HDD0_ROOT"

# I file di gioco: l'ISO montato in sola lettura.
export PS3_VFS_ROOT=${PS3_VFS_ROOT:-/tmp/uc_iso}

# La RSX vera comincia a consumare la FIFO nell'istante in cui `put` si muove:
# il drain va fatto li', non su un timer. Con il drain a orologio la passata
# entrava in un segmento che il titolo stava ancora riempiendo e ci restava.
export GCM_DRAIN_ON_PUT=${GCM_DRAIN_ON_PUT:-1}
export GCM_DRAIN_ONLY_PUT=${GCM_DRAIN_ONLY_PUT:-1}

# Il titolo chiude il segmento con un JUMP provvisorio e POI ci accoda sopra
# altri comandi prima di muovere `put`: quando la passata entra nel segmento
# l'uscita non c'e' piu'. Il fence atteso pero' sta sempre due parole prima di
# `put`, e da li' si riprende. Misura: fence per corsa 75 -> ~2900, e il
# blocco "Rendering is stuck on reference value N" passa da 5 corse su 5 a 1
# su 5 (e in quel caso la causa e' altrove: un task SPU fermo in WAIT_SIGNAL).
export GCM_STRAND_FIX=${GCM_STRAND_FIX:-1}

# Il policy module del job manager (WwsJob) viene ricaricato da zero a ogni
# dispatch, come fa il kernel SPURS vero quando cambia workload sulla SPU.
# Con il contesto PERSISTENTE (default del runtime) il manager, dopo il
# ResetList che il PPU fa a ogni fotogramma sulla job list dei frame (wid=4,
# 0x2A8D1C00), non trova piu' il lavoro accodato: il PPU arma la parola a
# 0x20000000 e gira per sempre (misurato: stallo dopo 57-62 fotogrammi in 3
# corse su 3; con FRESH 366-539 fotogrammi senza stallo in 2 corse su 2).
export SPURS_PM_FRESH=${SPURS_PM_FRESH:-1}

# Il synth audio (wid 7, ~1700 corse del PM al secondo) gira su un secondo
# thread kernel SPURS, come su una SPU separata: sullo stesso thread della
# geometria ne prendeva il 45% del tempo (sessione 28, SPURS_LANE_STAT).
export SPURS_KERN2_WIDS=${SPURS_KERN2_WIDS:-80}
# Sei lane per i workload MULTI-SPU (il titolo ne chiede 5 per la geometria,
# wid 12, lasciando un SPU agli altri). Le lane SPU sono il collo del fotogramma
# nel livello (s.34: 2/3/5/6 lane = 4,4/5,5/7,8/8,5 fps): sul Mac la sesta e'
# gratis. Dalla s.35 NON tocca i workload a una SPU (maxContention=1: wid
# 2/3/4/7/9/10): la BasicJobList del wid 3 (animazione/ragdoll) girata su 6
# lane si pestava i buffer e il PPU aspettava per sempre alla morte di Drake.
# s.41: il runtime lo limita comunque alle SPU dell'istanza (5 per Uncharted): la
# sesta lane dava al job della geometria una corsia inesistente (vertici spazzatura).
export SPURS_FORCE_SPUS=${SPURS_FORCE_SPUS:-6}
# s.38: lane SPU indipendenti (ogni lane sceglie da sola il workload), 12,6 -> 24-26,6 fps
export SPURS_LANE_SCHED=${SPURS_LANE_SCHED:-1}

# Occlusion query vere (visibility buffer Metal) e cache dei fragment program
# piena (8192). Lo stallo all'ingresso del livello che c'era con entrambi
# ("Rendering is stuck on reference value" / "Waiting for batch job list 0",
# sessione 28) era il watchdog delle DMA identiche che uccideva un job di
# geometria fermo su una terza forma di attesa del label: ora
# GCM_LABEL_UNSTICK_MS lo sblocca in ogni caso (3 corse su 3 pulite).
# RSX_OCCLUSION=0 e RSX_FP_CACHE_MAX=512 restano come A/B.
export RSX_OCCLUSION=${RSX_OCCLUSION:-1}
export RSX_FP_CACHE_MAX=${RSX_FP_CACHE_MAX:-8192}

# Le liste di comandi della scena le scrivono i job SPU in buffer CALLati
# dall'anello, con un JUMP-a-se-stessi in testa finche' il job non ha finito:
# la RSX vera resta ferma li'. Senza questo il walker risincronizzava
# sull'anello, saltava il buffer e il riferimento dietro la CALL, e il titolo
# aspettava per sempre ("Rendering is stuck on reference value 645").
export GCM_PARK_WAIT=${GCM_PARK_WAIT:-1}
export GCM_FLIP_FIFO=${GCM_FLIP_FIFO:-1}
# s.37: cellGcmSetWaitFlip non blocca il PPU (semantica del ferro: e' un'attesa
# per la RSX nella FIFO). Con il blocco la fase PPU e quella del walker erano
# in fila: 10,3 -> 14,6 fps. GCM_WAITFLIP_BLOCK=1 per tornare indietro.
export GCM_WAITFLIP_BLOCK=${GCM_WAITFLIP_BLOCK:-0}

# Impulsi CROSS/START sul pad: senza, il titolo resta fermo dove aspetta un
# tasto (avviso autosave, menu).
# Con PAD_SCRIPT ("75:x" = CROSS a 75 s) o con qualcuno alla tastiera della
# finestra (frecce, Spazio/X = CROSS, C = CERCHIO, Invio = START...) gli
# impulsi automatici vanno spenti: LBP_AUTOPRESS=0.
if [ -n "${PAD_SCRIPT:-}" ]; then export LBP_AUTOPRESS=${LBP_AUTOPRESS:-0}; else export LBP_AUTOPRESS=${LBP_AUTOPRESS:-1}; fi
# ...e basta impulsi dopo 28 s: "New Game" parte a ~22 s dal primo fotogramma
# e nel livello START mette in PAUSA (il fotogramma catturato era il menu di
# pausa su sfondo verde, sessione 29) e CROSS fa saltare/sparare.
export LBP_AUTOPRESS_UNTIL=${LBP_AUTOPRESS_UNTIL:-28}
# Solo CROSS negli impulsi (s.35): con il boot piu' veloce lo START finiva sul
# menu principale gia' interattivo e "New Game" non partiva (2 corse su 3);
# con il solo CROSS 4 boot su 4. La schermata "Press START" accetta CROSS.
export LBP_AUTOPRESS_NOSTART=${LBP_AUTOPRESS_NOSTART:-1}

exec "$D/uncharted" "$D/EBOOT.elf" "$@"
