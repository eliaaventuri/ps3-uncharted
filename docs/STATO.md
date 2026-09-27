# Stato del progetto

Ultimo aggiornamento: **26 settembre 2026** — sessione 47.

## Dove siamo

**Uncharted: Drake's Fortune gira su macOS fino al primo livello**
(`dive-boat`): menu, "New Game", salvataggio, livello con HUD, Drake intero,
morte e ripartenza dal checkpoint, audio senza click. Circa 28 fps (~7.000
fotogrammi in 200 s). Non e' ancora giocabile di continuo: resta uno stallo
intermittente (fotogrammi fermi, ~1 corsa su 10-15, vedi "Fronte aperto" nella
sessione piu' recente).

Le tappe, in una riga ciascuna (dettagli nelle sezioni delle sessioni; quelle
fino alla 37 stanno in `docs/STATO_STORICO.md`):

- s.17-20: il canale PPU->SPU (job WwsJob) funziona; drenaggio RSX continuo
  (~30 fotogrammi/s di comandi); `SPURS_PM_FRESH=1` toglie lo stallo post-caricamento.
- s.21-22: modalita' immediata della RSX implementata; render-to-texture,
  array di texture, DXT: prima immagine vera.
- s.23: tabelle globali da 8 slot nel runtime SPU rese per contesto (stallo
  intermittente); `CellPadInfo` corretto.
- s.25: switch mal liftato nel parser di testo (fix in `tools/ppu_lifter.py`):
  il caricamento finisce.
- s.26: il titolo aspettava un tasto (`LBP_AUTOPRESS=1`); arriva al menu.
- s.27: CR6 dei confronti vettoriali mai scritto dal lifter; VP del guest in
  MSL; `GCM_PARK_WAIT=1`; menu leggibile, "New Game" salva e carica il livello.
- s.29-30: livello a schermo con HUD; NV0039, costanti FP nell'hash, DRAW_ARRAYS
  a 256 vertici, maschera di colore.
- s.33: la SPU non ha NaN (helper float secondo CBEA): Drake intero.
- s.35: wid 3 (una SPU) girato su 6 lane: stallo alla morte; `SPURS_FORCE_SPUS`
  solo per workload multi-SPU.
- s.36-38: barriere nel lifter PPU, FPCR delle lane, `cellGcmSetWaitFlip` non
  blocca, lane SPU indipendenti (`SPURS_LANE_SCHED=1`), fetch vertici per piano:
  8 -> 28 fps.
- s.39: il flip aspetta la GPU (report delle occlusion query): scena non piu'
  mezza vuota; present senza copia.
- s.40: texture BC alla GPU compresse (picco memoria 14 -> 3,3 GB).
- s.41: niente job oltre le 5 SPU dell'istanza: vertici spazzatura 16.158 -> 0.
- s.42: costo fisso del runtime SPU ridotto (drain veloce, TTAS).
- s.43: audio, indice di lettura = slot dell'anello (click 22% -> 0,1%);
  lwmutex veri su macOS (crash DoGrain 2/31 -> 0/71).
- s.44: gli stalli "fotogrammi fermi" si riproducono col carico CPU (4/6
  corse); sono tutti la job list del wid 8 (0x10695C80) con la riga SPU 2 ferma.

Tastiera = pad 0 nella finestra (frecce, Spazio/X = CROSS, C = CERCHIO,
Invio = START); `PAD_SCRIPT` per le corse senza nessuno.

## Il materiale

- ISO: `~/Documents/Uncharted - Drake's Fortune (Europe, Australia) ....iso`
  (21 GB, **BCES00065**). Montato in sola lettura:
  `hdiutil attach -readonly -nobrowse -noverify -mountpoint /tmp/uc_iso "<iso>"`.
- Dati di lavoro **fuori dal repo**, in `~/Documents/Coding/uncharted-data/`:
  `EBOOT.elf` (decifrato), `analysis/`, `recomp/` (136 MB di C++), `gen/`,
  `obj/`, `uncharted` (il port), `build.sh`.
- Decifratura: RPCS3 `--decrypt` (build ufficiale arm64 nello scratchpad della
  sessione; va riscaricata se serve di nuovo).

## Numeri, identici a quelli upstream

L'eseguibile europeo **coincide** con la versione US documentata in
`docs/PPU_RECOMP.md`: 14.104 funzioni OPD, entry `0x10230`, TOC `0x7d3190`,
172 import su 12 librerie, 1 sola immagine SPU (39.608 B a `0x845000`).
Tutti i riferimenti upstream valgono per noi 1:1.

Il lift completo richiede **12 secondi**: 18.628 funzioni, 4 chunk, 2,16
milioni di righe di C++. La nota in `docs/PPU_RECOMP.md` sugli "88 MB in un
file" è superata: il lifter emette già chunk.

## Sessione 44 — stalli tipo A/B: un riproduttore (carico CPU) e un colpevole (job list wid 8)

**SPURS_WSTAT=1 non li nasconde.** Lotto A/B alternato (`scratchpad/misura/ablotto.sh`,
20 coppie da 160 s): WSTAT=0 4 fermi su 20, WSTAT=1 2 su 20. Il 25/25 della
s.43 era fortuna. Due dei fermi con assert del PM, entrambi `stopd @0x15B0`
con registri identici (r14 = 0x10695C80).

**La frequenza dipende dallo stato della macchina, non dalle variabili.**
Stesso binario (con la nuova diagnostica, spenta): 6/20 fermi alle 17-18;
poco dopo 0/10. `SPU_ASSERT_DMA=1` aveva dato 0/30 (sembrava che nascondesse
gli stalli: con il carico non li nasconde, 4/6). Morale: confronti A/B solo
alternati e nello stesso lotto; `lotto.sh` ora scrive il carico medio a fine corsa.

**Riproduttore:** `scratchpad/misura/stress_lotto.sh 8 <n> <pref> [VAR=..]`
(8 cicli occupati sulla CPU, carico ~15-25): fermi 2/8 e 4/6, assert del PM
in 3 corse su 8 (senza carico ~1 su 70). Assert viste sotto carico: @0x1654
(il tipo A della s.43, comando 0x1008EB00), @0x15B0, @0x15B4, @0x1640,
@0x423C e @0x8218 (overlay 217/218). Una famiglia di corse, non un bug solo.

**Il comando 0x1008EB00 non viene da SPURS:** LS 0xD00 e' la copia della riga
di intestazione della job list (GET 0x80, `pc 0x2558`), la cui prima parola
e' l'EA del PM. Il PM legge i comandi a 8 byte da LS 0xC00 saltando gli zeri
(`0x1BDC`: `brz r16`); con una lista comandi vuota/senza terminatore cammina
fino a 0xD00 e decodifica 0x1008EB00 come kRunJob ((f>>9)&7 = 5) -> r21 = 0 ->
assert @0x1654. Cioe': il job aveva una lista comandi vuota.

**Diagnostica nuova `SPU_ASSERT_DMA=1`** (spu_dma.h, spu_channels.c): per lane,
gli ultimi 24 GET/GETLLAR del PM (immagine 2); all'assert li stampa (`[adma]`)
col confronto LS vs memoria di ORA e LS 0xC00-0xD1F. Solo le voci della corsa
corrente (`run=` = contatore globale delle corse PM, `g_spu_adma_run`) hanno
un confronto sensato: la LS sotto 0x4000 si azzera a ogni corsa.
Cattura s44sd_6 (@0x15B0, wid 8): GETLLAR della lista 0x10695C80, GET delle
voci 0x10695D00 = `00400001 20316DF0 00400001 20319800`, GET 0x40 da
0x20319800 -> LS 0xC00 = `00000000 00000000 00000000 00000001 02000005
00000000 2050B150 0A40B150 00000131 ...`: **identica alla memoria di adesso**
(0 byte diversi). Quindi NON e' una lettura di dati non ancora pubblicati:
il blocco a 0x20319800 e' stabile ma non ha la forma di una lista comandi
(le liste buone iniziano `00000200 10410100 ...`). Ipotesi: voce della lista
vecchia (fotogramma precedente) o memoria del job gia' riciclata.

**Tipo B, cattura con FC_PEEK** (s44st8_1): intestazioni delle job list a +0x10
(u16 per SPU, 0xFFFF = SPU mai agganciata). Tutte le liste hanno le righe
uguali (`0001 0001`, `0982 0982`, `0050 0050`) tranne **wid 8 (0x10695C80):
`0050 0050 0001`** -- la riga della SPU 2 e' ferma a 1 mentre le altre sono a
0x50. E' il minimo che il main aspetta in `func_0074073C` (legge *(r3+4),
contatori a +0x10+2i). Nello stallo il wid 8 gira ~2.700 volte/s su tutti e
5 gli spuNum (imask 0x1F) senza trovare lavoro: la SPU 2 non recupera.
Tutte le assert @0x15B0 (s42_b3_1, s43_fc7, s44ab_3/16, s44sd_6) hanno
r14 = 0x10695C80: stesso workload.

### Fronte aperto (dopo la sessione 44)

0. Wid 8 (lista 0x10695C80, maxc 5, mai rimosso, a differenza di 12/14/15):
   perche' la riga SPU 2 resta indietro e perche' il PM trova voci che puntano
   a blocchi non-comando. Idea da verificare: una lane prelazionata dal
   sistema (host thread sospeso ms) che riprende dopo che il PPU ha fatto
   ResetList e riempito di nuovo la lista -- sul ferro una SPU non si ferma
   mai a meta' corsa. Strumenti: `stress_lotto.sh` + `SPU_ASSERT_DMA=1` +
   FC_PEEK sulle 10 liste (vedi s44st8_1.sample.peek). Chi scrive
   0x10695D00+8 e quando (YDKJ_WWATCH sul lato PPU)?
1. RPCS3 (oracolo): come il suo kernel SPURS sceglie lo spuNum per un
   workload con maxc 5, e se una SPU puo' agganciarsi alla lista una volta
   sola e poi non tornarci.
2. Resto del fronte della s.43 invariato.

## Sessione 43 — l'audio gracchiava: l'indice di lettura era un contatore, non lo slot dell'anello

**Misure prima** (`AUDIO_RATE=1`, `AUDIO_RAW=<file>`, nuove, solo POSIX):
il mixer va al ritmo giusto (187,4 blocchi/s contro 187,5; coda SDL
stabile 21-32 ms: l'ipotesi "il mixer corre piu' veloce del ferro" e'
SCARTATA), tutti i blocchi hanno segnale (RMS ~0,1), ma il 22% dei confini
fra blocchi da 256 campioni ha un salto (>5x la variazione interna) contro
il 2% su confini finti a meta' blocco: ~40 click al secondo, spettrogramma
a righe verticali. Nessun blocco muto: non e' un underrun.

**Causa.** Il thread `syn_tick_timer_thread` (entry OPD 0x7C91D8 ->
`func_0071ECF8`, `sys_timer_usleep(1e6/rate)`, rate 0x3C=60 Hz in gioco,
0xF0 a vuoto) chiama `func_0071E9B8`: legge l'indice a `readIndexAddr`
(`ld` da *0x108DC884, salvato in *0x10171CB8) e produce blocchi finche'
il proprio slot 0..7 (*0x108DC8E0, azzerato a 8) non e' UGUALE a
quell'indice. Noi pubblicavamo `read_index` che cresce all'infinito:
dopo 8 blocchi l'uguaglianza non si verifica piu' e il synth scriveva a
ogni tick tutti i blocchi che riusciva, sopra quelli non ancora suonati.
RPCS3 (`cellAudio.cpp`: `indices[..] = port.cur_pos`, `position()` e'
`% num_blocks`) pubblica lo slot. Fix: `read_index % nBlock` in
`libs/audio/cellAudio.c` (vale anche per Windows: e' la semantica del
firmware; il watchdog di LBP confronta solo che cambi).

**Dopo:** salti ai confini 0,1% = controllo 0,1%; piattezza spettrale
0,29 -> 0,15. WAV prima/dopo in scratchpad della sessione (non nel repo).
Fotogrammi a 200 s: 4.013 (crash), 6.867, 7.923, 6.771 (s.42: 6.750-7.200);
a 160 s 5.209-5.942.

**Correzione allo stato della s.42:** `SCREAM: in DoGrain but handler's
SH.Sound member is NULL!` NON compare al boot in tutte le corse (al boot
c'e' solo il banner `SCREAM PS3 (c)2005`): in 48 log compare solo nelle
corse del crash. Il crash e' sul thread del synth: DoGrain
(`func_00714F28`) trova `handler+4 == 0`, stampa con `func_0070A2E0` che
poi chiama un hook indiretto (*(*(r2-0x7388))) -> host 0x0. Sul ferro
dopo la stampa c'e' un `b .` (0x7150A4): il thread si appenderebbe.
Frequenza: 1/28 corse in s.42, 1/10 dopo il fix (s43_fs1..10): non
significativo, ma da tenere d'occhio -- il fix cambia il ritmo del synth
(~3 blocchi per tick invece di "tutti quelli possibili").

**L'altro fallimento raro della s.42** (`stuck on reference value`, corsa
s42_b3_1): l'assert del policy module WwsJob a 0x15B0 e' il primo della
banca di `stopd`, raggiunto da `brz $r5,0x15B0` a 0x33FC: il magic
0xC0DEC0DE a [base del job + 0x20] non c'e' -- la base del codice del job
letta da LS 0x1330 vale 0 (scritta a 0x23FC da `r10 = roti r31,10`). Un
job partito senza codice caricato. Solo una corsa, niente riproduzione.

### Seconda parte: lwmutex veri su macOS, e lo stallo "fotogrammi fermi"

**Scoperta:** su macOS `sys_lwmutex_lock/trylock/unlock` (i gestori ctx in
`runtime/ppu/ppu_sysprx.cpp`, registrati prima della tabella generica) erano
NO-OP: il semaforo per indirizzo esisteva solo nel ramo `_WIN32`, il ramo
POSIX era `lwm_sem() { return nullptr; }`. SCREAM protegge il tick con un
lwmutex (pool a *(r2-0x7164) = 0x10A56F88, voci da 0x20 a +8; lock/unlock/
trylock = `func_00723390`/`func_00723358`/`func_007233D0`; il nome
"snd_tick_lock_sema" non e' un sys_semaphore -- zero semafori creati).
Fix: ramo POSIX con mutex+cond per indirizzo (`LwmPx`), stessa semantica
del ramo Win32 (ricorsione via owner/recur, ETIMEDOUT, EBUSY, create che
libera, lwcond_wait che rilascia e riprende). Diagnostiche: `LWM_CONTEND=1|2`
(prese contese: ~300 in 160 s su due mutex, 0x109ECC48 e 0x2A870F28),
`[LWM-STALL]` (attesa > 3 s: chi tiene il lucchetto e da quale lr),
`[LWM-LOST]` (sveglia persa recuperata). Mai visti STALL o LOST.

**Conteggi (corse da 160 s):** prima del fix 31 corse = 2 DoGrain, 1
"stuck on reference", 1 fotogrammi fermi; dopo 31 corse = 0 DoGrain, 0
stuck, 3 fotogrammi fermi (lwmb1 a 68 s, lwmb3 a 115 s, lc4 a 99 s), 1 corsa
rimasta nel menu (l'autopress ha mancato il tasto: difetto dell'imbracatura,
non del gioco). Non significativo in nessuna delle due direzioni.

**Fotogrammi fermi, catturato dal vivo** (`scratchpad/misura/framecatch.sh`,
campiona con `sample` quando i fotogrammi non avanzano per 20 s; s43_fc7):
il main (tid 1) gira in `func_0014F528` @0x14F9CC: `while (*0x10511F2C < r27)
sys_timer_usleep(10)` -- aspetta il PROPRIO contatore dei vblank (r27 = 0/1/2
dall'intervallo di vsync); tutte le lane SPURS dormono. Chi scrive il
contatore (YDKJ_WWATCH): il gestore vblank del titolo (valore 1) e il main
(0 dopo l'attesa); nessuna SPU (SPU_DMA_EAWIN/SPU_ATOM_EA: zero). Il gestore
vblank si esegue solo in `ppu_gcm_pump` (a ogni import HLE di qualunque
thread, o dopo 4096 letture consecutive dello stesso indirizzo). Nuova
diagnostica `GCM_PUMP_STAT=1`: ogni 300 vblank del ticker, gestori
eseguiti e tid dell'ultimo (normale: consegnati da tid 1, 15, 18, 19).
Aperto: nello stallo il gestore smette di essere consegnato, o gira ma il
contatore non basta? Serve una cattura con GCM_PUMP_STAT=1.

**Risposta (catture pc25 e pc31, con GCM_PUMP_STAT=1):** il gestore vblank
gira regolarmente anche durante lo stallo (300 esecuzioni ogni 300 impulsi,
contatore a 0x618 in pc31); il gestore del titolo e' `func_0014EA80`
(counter++ non atomico); soglia a 0x104296B8+8 = 2 (30 fps), bandierina
0x10503F48+0x1C. IL VBLANK NON C'ENTRA: la cattura fc7 aveva preso il main
di passaggio. Nei due stalli il main e' nel controllo "job list finita"
(`func_0074073C`: minimo dei 5 contatori per SPU >= obiettivo), chiamato da
`func_0047CC40` (pc25) o `func_001AD590`/`func_007417D8` (pc31). Due tipi:
- **A (pc25; e s42_b3_1):** subito dopo AddWorkload(14/15) il PM asserisce
  (`stopd @0x1654`, da `brz $r21` a 0x23DC; in s42 `@0x15B0` da 0x33FC, magic
  0xC0DEC0DE): il comando di caricamento letto in LS (r16, a LS 0xD00)
  vale 0x1008EB00 = l'EA del PM stesso, cioe' i comandi del job sono stati
  copiati da un EA dentro la tabella dei workload di SPURS. Il job e' perso
  e la lista non si chiude. Le lane restano in `resv_lock_spin` nel PM.
- **B (pc31, lc4, lwmb1, lwmb3):** nessuna assert, lane tutte a dormire,
  il main aspetta la lista. Diagnostica nuova `SPURS_WSTAT=1`: ogni 5 s per
  wid in uso abil/stato/shutdown/rdy/sig, avvii, kick, avvii con lavoro,
  spumask, idle, imask, maxc, arg.
**Bilancio con gli lwmutex veri: 71 corse** (160 s): 0 DoGrain, 0 SIGSEGV,
0 "stuck on reference", 5 fotogrammi fermi (tipo A 1, tipo B 4). Prima:
31 corse, 2 DoGrain + 1 stuck + 1 fermo. Con `SPURS_WSTAT=1` acceso 25 corse
su 25 pulite (senza: 2 fermi su 15) -- p ~0,13, forse un heisenbug di
tempi: la lane 0 chiama spurs_ns_now() a ogni giro.
`framecatch.sh` ora legge memoria guest dal processo vivo (`FC_PEEK="ea..."`,
lldb) e rifiuta di partire senza ISO montata. ATTENZIONE: dopo un riavvio
del Mac /tmp/uc_iso sparisce e le corse restano nel menu (~24.500
fotogrammi vuoti in 160 s): 16 corse buttate in s.43.

### Fronte aperto (dopo la sessione 43)

0. Fotogrammi fermi tipo A/B (sopra): ~1 corsa su 10-15.
1. (Probabilmente chiuso dagli lwmutex veri: 0/71) Crash DoGrain (SH.Sound NULL sul thread del synth): corsa fra lo stop
   di un suono e il tick? Lock SCREAM = `func_00720B68`/`func_00720B20`
   (trylock via `func_007416B0`). Servono piu' corse per la frequenza.
2. Contesa `resv_lock`, fallimenti rari della s.42, codice SPU liftato.
3. Elia: ascoltare l'audio in gioco (ora senza click?) e giocare oltre il
   checkpoint.

## Sessione 42 — il costo fisso del runtime SPU: ~26 -> ~28 fps

**Dove andava il tempo delle lane** (`sample` a 100 s, livello di
riferimento, 6 lane al 85% occupate, 19.000 campioni attivi su 10 s):
codice dei job liftati 48%, canali/DMA 24%, trampolino e dispatch 17%,
policy module 6%. Cioe' il 40% del lavoro delle lane era runtime, non
codice del gioco. Strumenti nuovi in
`scratchpad/misura/`: `selfunder.py <sample> <antenato>` (tempo self sotto
un simbolo, attese separate), `incl.py` (inclusivo), `callers.py`.

**Modifiche** (tutte equivalenti nella semantica, riviste riga per riga):
1. `SPU_DRAIN` (spu_context.h) -> via veloce `spu_drain_fast` (spu_drain.c):
   le sei chiamate fuori linea per salto (sonde, lockstep, guardia di
   profondita', retlink/tailret) sostituite dagli stessi controlli in
   linea. Il ciclo completo resta (`SPU_DRAIN_FULL`), scelto da solo con
   qualunque variabile di sonda, lockstep o SPU_TAILRET=1.
   `SPU_DRAIN_FULL=1` per l'A/B. Il log dice `[spu-drain] ciclo veloce`.
2. `spu_indirect_branch`: via veloce (lookup + musttail) prima delle dieci
   sonde; `SPU_IB_FULL=1` per l'A/B. Guadagno misurato ~1% (fotogrammi a
   130 s 4.912 contro 4.875, 4+4 corse).
3. `spu_irq_regs_maybe_restore` chiamata solo se `irq_save_valid`.
4. `mfc_submit` (spu_dma.h): i contatori del watchdog "DMA identiche" per
   thread invece che globali (sei lane scrivevano la stessa riga di cache
   a ogni DMA).
5. `spu_overlay_note_get`: uscita subito per GET < 512 byte quando non ci
   sono sorgenti a EA (Uncharted: solo firme, 29).
6. `resv_lock` (atomici GETLLAR/PUTLLC): test-and-test-and-set con `yield`,
   attesa fuori linea in `resv_lock_spin`. Il profilo dopo i punti 1-5 dava
   `resv_lock_spin` a 1.058 campioni su 17.000: contesa vera fra le lane
   (il PM di WwsJob fa atomici sulle stesse righe del kernel SPURS).

**Misure** (200 s, `fpsmed.sh` 100-190 s, `fotogrammi` = ultimo
RSX_FRAMESTAT a 200 s):

| binario | fps mediani | fotogrammi a 200 s |
|---|---|---|
| ciclo completo (SPU_DRAIN_FULL=1) | 25,2 / 25,7 | 6.450 / 6.350 |
| solo punto 1 | 27,0 / 26,4 / 27,2 / 25,7 / 25,0 / 26,4 | 6.400-6.650 |
| punti 1-6 | 26,5 / 29,6 / 29,1 / 25,1 / 27,0 / 29,7 | 6.750-7.200 |

Le mediane ballano +-2 fps col contenuto (lo sapevamo dalla s.39); i
fotogrammi a 200 s sono piu' stabili: +9% dal ciclo completo ai punti 1-6.
`garb=0` in tutte le corse.

**Fallimenti da tenere d'occhio (APERTO).** Con i punti 2-6 tre corse su
20 sono finite male, con tre sintomi diversi e tutti fra 70 e 105 s:
- SIGSEGV del PPU a host 0x0 in `func_00714F28+0xCF4`, subito dopo il
  secondo `SCREAM: in DoGrain but handler's SH.Sound member is NULL!` (il
  primo compare in TUTTE le corse al boot e non fa danni);
- `Rendering is stuck on reference value 3816` con `[spu-assert] img=2
  stopd @0x015B0` e 12 `[stale-call]`, durante il caricamento di temple-0a;
- fotogrammi fermi a #3300 senza nessun messaggio (SPU vive, task in
  WAIT_SIGNAL).
Con il solo punto 1: 0 su 6 (+2 col ciclo completo). La bisezione sul
punto 2 (8 corse da 130 s, 4 con e 4 senza) e' tutta pulita, e le ultime
4+4 corse da 200 s sono tutte pulite. 3/20 contro 0/8 non basta per dare
la colpa: ipotesi di lavoro = corse gia' presenti nel port che un gioco piu'
veloce incontra piu' spesso (stato di gioco diverso allo stesso istante).
Se ricompaiono: `SPU_IB_FULL=1` e un binario senza il punto 6 sono le due
leve di A/B.

**Cosa resta nel profilo delle lane** (dopo 1-5): job liftati ~50%,
`resv_lock_spin` 6% (prima del punto 6), `spu_drain_fast` 5%, DMA
(mfc_do_transfer + memmove) 6%, `mach_absolute_time` da `SPU_RdDec` 1,6%
(`SPU_DEC_CACHE=1` c'e' ma non e' misurato qui).

### Fronte aperto (dopo la sessione 42)

1. Misurare la contesa rimasta su `resv_lock` dopo il TTAS (profilo); se
   resta, lock per riga (hash dell'EA) con il lock globale solo per il PPU.
2. I tre fallimenti rari qui sopra.
3. Il codice liftato stesso (meta' delle lane): cache dei registri con
   analisi di flusso (s.36: la versione ingenua perde).
4. Giocare oltre il checkpoint con la tastiera (Elia); audio: si sente?

## Sessione 41 — i vertici spazzatura erano la sesta SPU: spuNum oltre le SPU dell'istanza

**La causa.** Il mistero della s.40 (offset dei vertici "indirizzo - 1",
0x6B05C29F, e dati 0xCDCD) si scioglie leggendo il pacchetto intero attorno
al comando (`RSX_VAODD_SCAN=1`, ora stampa 40 parole della FIFO): nei
blocchi cattivi `SET_SEMAPHORE_OFFSET` (0x1D6C) vale **0xFF0** invece di
0x480+16*corsia, il label (0x1D74) vale 0x000005FF/0x20FF/... (posizioni
relative, non EA 0x20Exxxxx) e l'offset dell'attributo 0 e' 0x6AE00000 +
label del blocco prima (partenza 0x6ADFFFFF = posizione -1). Cioe' il job
della geometria (ovl 215) lavora su una corsia inesistente. Le corsie sono
5 (8 MB / 5 = 0x199980, gia' visto in s.32) perche' Uncharted crea SPURS con
**5 SPU**; `SPURS_FORCE_SPUS=6` (run.sh, dalla s.34) dava ai workload
multi-SPU anche spuNum 5. Anche gli stride dispari (59/31/47) e il valore
del menu 0x6A5FFFFF vengono da li'.

Come ci si e' arrivati (per non rifarlo): `YDKJ_VWATCH` sul valore = 0
scritture PPU; `RSX_VAODD` con `@fifo` = il comando sta nella memoria dei
job SPU (0x2050xxxx-0x2060xxxx); `SPU_PUT_VAL=<hex>[:mask]` (nuovo,
`spu_dma.h`: PUT e GET che portano quel valore, passo 2 byte) = lo scrive
l'ovl 215 senza averlo mai letto (il colpo dell'immagine 1 = edge zlib
inflate era una coincidenza); ovl 215 = interprete di record da 16
mezze parole (tabella dei gestori a LS 0x15230), niente costanti utili nel
disassemblato; `SPU_INTERP_OVL=215,0` da' lo stesso valore (non e' il
lifter). Nota: l'immagine 1 (inflate) scrive direttamente in VRAM
0xCA/0xCB (pacchetti caricati in memoria locale).

**Il rimedio** (`cellSpurs.c`, `spurs_wkl_maxcont`): la contention non
supera mai `nSpus` dell'istanza (byte +0x76), qualunque sia
`SPURS_FORCE_SPUS`. Le 6 lane restano: la sesta prende gli altri workload.

**Misure** (200 s, screenshot ogni 5 s da 60 s, `classify2.py`):

| | prima (6 SPU) | dopo |
|---|---|---|
| `garb=` in tutta la corsa, scenario incontro | 16.158 (dopo f#3400) | 0 |
| `garb=` scenario di riferimento | 100-200 / 50 fotogrammi (s.40) | 0 |
| screenshot difettosi, incontro | 7/29 (3 ombra/cielo, 4 nero/bianco) | 6/29, tutti bianco/nero = danno/morte del gioco |
| screenshot difettosi, riferimento | 3/29 (s.40) | 2/29, bianco (danno) |
| fps mediani 100-190 s, incontro | 26,7 | 21,1 |
| fps mediani 100-190 s, riferimento | 28,6 (s.40) | 24,4 |

**Il prezzo: -15/20% di fps.** La sesta lane "gratis" della s.34 era in
parte lavoro buttato (blocchi scritti su una corsia fantasma). Ora il job
della geometria ha 5 lane come sul ferro; per recuperare serve la velocita'
del codice SPU liftato (fronte 2).

**Diagnostiche nuove:** `SPU_PUT_VAL=<hex>[:<mask>]` (PUT/GET SPU che
trasportano una parola, allineata a 2); `RSX_VAODD` stampa `@fifo <EA>`;
`RSX_VAODD_FROM=<fotogramma>`; `RSX_VAODD_SCAN=1` (byte a VRAM+offset&28bit
e al giro in memoria principale, piu' 40 parole della FIFO attorno).

### Fronte aperto (dopo la sessione 41)

1. Velocita' del codice SPU liftato (il job della geometria su 5 lane e' il
   collo: 21-24 fps).
2. Giocare oltre il checkpoint con la tastiera (Elia); audio: si sente?

## Sessione 40 — texture BC caricate compresse (picco 14 -> 3,3 GB); i difetti grossi dipendono dallo stato del gioco

**1. Le texture BC1/2/3 vanno alla GPU compresse (`rsx_metal_backend.m`,
`bc_native`).** Prima ogni DXT veniva decodificata in RGBA8 dal walker
(`rsx_texture_decode_bc`): x8 di memoria per le DXT1, x4 per le DXT5, e
la decodifica sul thread che registra i draw. I blocchi DXT in memoria
guest sono gia' nel layout della GPU (il decoder li leggeva cosi' com'e',
senza scambio di byte; RPCS3 li copia come u64) e le GPU Apple li
supportano (`supportsBCTextureCompression`). Ora `replaceRegion` direttamente
dalla memoria guest con `MTLPixelFormatBC{1,2,3}_RGBA`, `bytesPerRow` =
`tl.row_bytes`; le viste con remap ereditano il formato. `RSX_TEX_BC=0`
torna alla decodifica (automatico con `RSX_TEX_SAVE`/`RSX_TEX_DBG`, che
leggono i texel). Contabilita' nuova nel framestat: `bc=` (texture caricate
compresse), `texMB=` (somma degli `allocatedSize` delle texture vive nella
cache, `texcache_release` la scala alle tre evizioni).

Misura (`scratchpad/misura/memshots.sh 200 <log> <pfx> 100 10 185`, vmmap
a 185 s; 3 corse per variante):

| | decodifica (RSX_TEX_BC=0) | BC nativo |
|---|---|---|
| texMB di picco (caricamento, f#2450-2850) | 8.096 -> 12.674 | 679 -> 2.014 |
| texMB in gioco (f#4850+) | 250-270 | 49-51 |
| footprint fisico di picco (vmmap) | 14,1 / 15,0 / 14,5 GB | 3,3 / 3,3 / 3,3 GB |
| footprint a 185 s | 2,0 GB | 1,7 GB |
| IOAccelerator (graphics) residente a 185 s | 365 MB | 142 MB |
| decodifiche al caricamento (f#2050-2850) | 6.700 (1.200 Mpx) | 412 (6 Mpx), 17.500 bc |
| f#2450 -> f#2850 (caricamento) | 19,2 s | 16,7 s |
| fps mediani 100-190 s (3 corse) | 27,8 / 24,4 / 27,5 | 25,9 / 23,3 / 28,5 |

I fps dipendono dal contenuto (s.39), non dal binario: le corse a 23-24
sono quelle dell'incontro (v. punto 2). Il picco a 12,7 GB era il
caricamento del livello: il titolo compatta il proprio heap texture
(NV0039) e la stessa texture compare a offset diversi; le voci vecchie
restano 600 fotogrammi (sweep) e nel frattempo ognuna e' RGBA8 piena.
Screenshot BC contro decodifica confrontati a pieno formato: identici
(casse, ruggine, ponte, HUD).

**2. Le corse "cattive" non c'entrano con le texture.** Con 29 screenshot
per corsa (ogni 5 s da 60 s; `classify2.py` aggiunge `alto` = frazione di
pixel color sfondo nel quarto alto del fotogramma, DIFETTO sopra 25%: il
cielo sparito si vede li' anche quando il totale resta sotto il 45%):
base 1/11, base2 13/29, base3 3/29 (nero/bianco = morte di Drake); bc
4/11, bc2 17/29, bc3 0/29. I difetti sono due, sempre insieme nella stessa
corsa: poligoni giganti color ombra o neri che coprono mezzo schermo, e
cielo/mare/ponte spariti (resta lo sfondo grigio-blu con le casse). Le
corse cattive sono quelle in cui il gioco entra nell'incontro
(`GameTaskSubnodePlay(): Playing task 'dive-boat-encounter-first-wave'`,
stati di copertura `s_cover-low-*`, 3.600 draw/fotogramma di picco): la
sola differenza nei log. NON e' il tetto dei draw (0 `dropped`), non sono
command buffer falliti, i `[flip-late]` sono uguali (406-414 oltre 8 ms su
7-8.000 flip, 263 rimandati al completed handler). Il "triangolo gigante
raro" della s.39 e' questo stesso difetto: raro solo perche' la corsa di
riferimento di solito non entra nell'incontro. Candidati: i pacchetti
CALLati delle regioni 0x0A400000/0x0A500000 (parkgen, s.31) o l'anello dei
blocchi mesh (s.32) consumati con contenuto vecchio quando i job SPU sono
piu' carichi; `[parkgen] ... non e' un park` (prima parola 3F800000 =
float) compare in tutte le corse. Per riprodurlo serve una corsa che entri
nell'incontro (PAD_SCRIPT che faccia avanzare Drake) e `RSX_GARBAGE`
raffinato con la posizione dall'input del VP.

**Altro:** `graphify-out/` (544 file tracciati, output generato) risulta
cancellata dal disco in questa sessione senza che nessun comando nostro
l'abbia toccata; non committata la cancellazione.

**3. Riprodurre l'incontro a comando.** `PAD_STICK=128,0,128,128
PAD_STICK_FROM=65 PAD_STICK_UNTIL=85` (nuovo `PAD_STICK_UNTIL` in
`cellPad.c`): Drake cammina avanti 20 s ed entra nell'incontro in tutte
le corse (`first-wave` nel log). Con questo scenario i difetti sono 4-19
screenshot su 29 (`classify2.py`), contro 0-3 senza camminare.

**4. Due cause trovate, una sistemata.**
- **(sistemata) Draw con la coppia (FP, VP) non ancora compilata.** La
  compilazione MSL e' asincrona e costa 1,6 s in media nel livello (640
  compilazioni in 200 s): intanto il draw ricadeva sul VS incorporato
  (MVP fissa da c0..3, e dalla s.38 senza nemmeno i MtlVertex) e diventava
  un poligono da 30-60 milioni di frammenti (`RSX_BIGDRAW`, f#3000: 50 draw
  cosi' in un fotogramma, tutti `vp=00000000`). Ora `pso_for` salta il draw
  finche' la coppia non e' pronta (`saltati=` nel framestat, ~33.000 in 200
  s); `RSX_VP_FALLBACK=1` rimette il vecchio percorso. Dopo: 0 draw giganti
  senza VP.
- **(aperta) Blocchi di vertici con posizioni spazzatura.** `RSX_GARBAGE=1`
  conta per blocco di framestat (`garb=`): ~100-200 draw ogni 50
  fotogrammi nel livello, 1.100-1.900 al caricamento. Il valore tipico e'
  0xCDCDCDCD (-431602080.0: riempimento di memoria liberata della build di
  debug del titolo). `RSX_VP_DBG_STAT=3` (nuovo: una riga solo per i draw con
  spazzatura) + `RSX_VA_RECHECK=1`: in molti draw la spazzatura c'e' GIA' al
  record e sparisce al present (chi scrive arriva dopo che il walker ha
  letto), in altri compare dopo (memoria riusata: innocuo, i vertici sono gia'
  copiati). TUTTI gli offset dell'attributo 0 di questi draw sono dispari
  (== 3 mod 4, es. 0x6B05C29F, `RSX_VAODD=<n>` li stampa con i metodi
  precedenti): location LOCAL con valore > 256 MB, che da noi fa il giro a 32
  bit su 0x2B05C29F in memoria principale. Nessun job SPU scrive in
  0x2AE5xxxx-0x2B08xxxx (`SPU_DMA_EAWIN`: solo GET del job 218, e PUT di
  212-214 in 0x2B14B-0x2B153): li scrive il PPU. Nel menu la stessa forma:
  attr0=0x6A5FFFFF, attr8=+12, attr3=+16 (= indirizzo - 1 con pos/tc/colore
  a 0/12/16).

  **Provato e scartato:** (a) la base dei vertici (NV4097 0x1738, RPCS3
  `get_vertex_offset_from_base`, somma + maschera a 28 bit): il titolo non
  la scrive mai, e la sola maschera a 28 bit (lettura in VRAM 0xCB05...)
  moltiplica la spazzatura x6 (881.000 contro 150 per blocco) -- lo stato
  resta registrato in `rsx_state.vertex_data_base_offset` ma il fetch non lo
  usa; (b) leggere gli offset == 3 mod 4 a +1 (allineati): nei singoli
  blocchi sospetti la spazzatura va a 0 (draw#1543: 160 -> 0, bbox -3,9..27,2
  sensato), ma sul totale sale x7 (1.094 per blocco) e il cielo sparisce
  piu' spesso: molti flussi dispari sono giusti cosi'. (c) `GCM_SEMA_STAT`:
  il titolo non usa l'ACQUIRE del semaforo (0 in 110 s).

  **Da capire:** chi produce l'offset "indirizzo - 1" (0x2B05C2A0 +
  0x3FFFFFFF = 0x6B05C29F, cioe' ptr - 0xC0000001) e perche' su una parte
  dei blocchi i dati arrivano dopo il walker. Sul ferro quel heap sta
  probabilmente in VRAM (0xC.......) e l'offset e' piccolo: da noi il heap e'
  in memoria principale. Prossimo passo: trovare nel PPU liftato chi scrive
  0x6B05xxxx nel command buffer (PS3_TTY_BT / lldb_guest_bp su
  cellGcmAddressToOffset o sul calcolo) e da dove viene il puntatore
  0x2B05C2A0 (sys_memory / mmapper / cellGcmMapLocalMemory?).

**Diagnostiche nuove:** `RSX_BIGDRAW=<frammenti>` (+`_FROM`, `_SURF`): a GPU
finita i draw con piu' frammenti passati (contatori di visibilita');
`RSX_GIANT=<k>` (+`_FROM`, `_MIN`): triangoli con un lato > k volte la
mediana del draw; `RSX_GARBAGE_FROM`; `SPU_WRING=dyn` (la finestra del wring
la chiede il backend al primo blocco spazzatura, `g_spu_wring_req`);
`memshots.sh` scrive `<log>.shots` con l'orologio monotono e
`shotframe.py <log>` da' il fotogramma di ogni screenshot.

**Numeri finali (scenario di riferimento, senza camminare):** 26/29
screenshot puliti (3 = morte di Drake), 28,6 fps mediani, picco 3,3 GB.

### Fronte aperto (dopo la sessione 40)

1. Gli offset dei vertici "indirizzo - 1" e i dati scritti dopo il walker
   (punto 4, aperta): scenario `PAD_STICK_FROM=65 PAD_STICK_UNTIL=85`.
2. Velocita' del codice SPU liftato (cache dei registri con analisi
   vivi/sporchi, u128 in ordine SPU).
3. Giocare oltre il checkpoint con la tastiera (Elia); audio: si sente?

## Sessione 39 — il flip aspetta la GPU: la scena non sparisce piu'; present senza copia (4,2 -> 2,0 ms)

**Misura nuova: la mediana dei fps (fpsmed.sh, 100-190 s) dipende dallo
stato del gioco, non solo dal binario.** Stesso binario s.38, quattro corse:
22,9 / 24,6 / 27,4 / 23,1 fps mediani; i draw per fotogramma nella finestra
erano 2.560 / 2.470 / 1.940 / 2.580 (fin1/fin2 della s.38: 2.120 / 1.890 a
27 fps). ~1.900 draw = 27 fps, ~2.500 = 23: la finestra a orologio copre
contenuti diversi (dove sta Drake, quanti nemici). Per le modifiche al
present la misura giusta e' la durata del present per fascia di draw
(`scratchpad/misura/present.py <log> [t0 t1]`, eventi `present-in/out` di
`GCM_TIMELINE`); per i difetti visivi gli screenshot ogni 5 s
(`shots2.sh <s> <log> <pfx> <da> <passo> [VAR=..]`) classificati da
`classify.py` (frazione di pixel color "ombra" (48-63,48-63,64-79), neri,
bianchi: DIFETTO sopra 45/40/40%).

**1. Present senza copia (`rsx_metal_backend.m`, `ring_alloc`,
`vpd_next_slot`).** Il flusso degli attributi (`s_vpd`, ~80 MB a fotogramma)
e i blocchi di costanti (`s_cbs`, fino a 20 MB) stanno in un anello di
`MTL_MAX_INFLIGHT+1` = 4 slot `newBufferWithBytesNoCopy` su memoria `mmap`
(pagine toccate solo se usate; 8 slot massimi, `RSX_VPD_RING=<n>` per la
prova, `=0` torna alla copia); il present lega lo slot e a fine fotogramma
si passa al successivo: con al massimo 3 fotogrammi in volo lo slot nuovo e'
libero. Present per fascia di draw (mediana, ms): copia 3,84 / 4,16 / 4,80
(1.500-2.000 / 2.000-2.500 / 2.500-3.000 draw), solo vpd 1,97 / 2,65 / 3,00,
vpd+cbs **1,90 / 2,02 / 3,26**. Prima di fidarmi ho creduto per sei corse
che l'anello producesse poligoni giganti (v. punto 2): erano gia' li' senza
anello. Diagnostiche restate: `RSX_VPD_CHECK=1` (ogni draw legge solo i
vertici che ha scritto: 0 violazioni), `RSX_GARBAGE=1` (posizioni NaN/enormi
o bbox > 20000 per draw: da raffinare, ~12 draw per fotogramma sono VP che
non hanno la posizione nell'attributo 0, non spazzatura).

**2. Il difetto vero: il flip chiuso prima che la GPU avesse finito.** Tre
screenshot su tre puliti senza anello, poi il quarto: 7 su 10 con ponte,
mare e cielo SPARITI (resta lo sfondo grigio-blu, casse e personaggi
normali), o tutto bianco (l'esposizione automatica che rincorre un
fotogramma vuoto), o un poligono gigante. Provato e scartato: anello a 8
slot, coda azzerata dopo i dati usati, memoria allocata da Metal, ritardo
artificiale di 2,5 ms nel present, impronta dello slot al commit e al
completamento (0 differenze su 3.500 fotogrammi: nessuno scrive mentre la
GPU legge), anello senza rotazione, `mmap` puro: il difetto restava in
tutte, e alla fine anche in una corsa SENZA anello (2 fotogrammi su 3).
La traccia dell'anello dei job SPU (`GCM_RING_TRACE=1`, analisi
`scratchpad/misura/ringan.py`) e' pulita: 5 lane, 50.000 blocchi, gap
massimo 0,93 giri, nessun sorpasso, nessuna valvola. Quello che sparisce e'
la geometria grande con le bounding box = le occlusion query: il job 215
legge i report del fotogramma N subito dopo il flip N; in
`cellGcmSetFlipCommand` (flip-done-late, s.37) il flip N si chiudeva dopo
un'attesa della GPU di AL MASSIMO 8 ms e i report (scritti dal completed
handler del command buffer) arrivavano dopo: il titolo leggeva 0 = occluso.
Misurato: attesa media 2,6 ms, massimo 27 ms, oltre 8 ms nell'11% dei flip.
Prova con il tetto a 200 ms: 10 su 10 puliti -- ma bloccare il PPU fino a
27 ms ha dato "Rendering is stuck on reference value 4009" in 1 corsa su 2.

**Fix (`cellGcmSys.c`):** il flip in ritardo aspetta la GPU fino a
`GCM_FLIP_GPU_WAIT_US` (8000) come prima; se non ha finito, il gestore resta
DOVUTO (`s_flip_late_due`) e lo consegna `cellGcm_gpu_idle()`, chiamato dal
completed handler del backend quando i fotogrammi con report in volo tornano
a zero (`s_flip_owed`, consegnato dalla pompa alla prossima chiamata HLE o
spin, in aggiunta ai bit 2/4). Niente attesa lunga sul PPU, niente flip
perso, ordine del ferro (flip fatto => report scritti). Con
`GCM_TIMELINE`: eventi `flip-late-due` e `flip-done-gpu`. Statistica ogni
1000 flip: `[flip-late] attesa GPU: ... rimandati al completed handler: n`
(172 su 6.000 in una corsa). Risultato: **13 su 14 e poi 14 su 14
screenshot puliti** (28,5 fps mediani nella corsa). Il tick del flip
(cellGcmTickFlip) aspettava gia' la GPU e riprovava al tick dopo: giusto,
invariato.

**3. Resta un difetto raro: un triangolo gigante scuro** (1 screenshot su
28: `/tmp/s39_fix1_140.png`, cielo e mare intatti, un triangolo color ombra
sul quarto in basso a destra). Non e' il culling; e' un draw con un vertice
sbagliato, probabilmente nel passaggio delle ombre o dei volumi di luce.
Da cercare con `RSX_GARBAGE` raffinato (posizione dall'input del VP, non
attributo 0) e screenshot fitti; il candidato storico e' l'anello dei
blocchi mesh (s.32), ma la traccia dice di no.

**Altro:** l'audio arriva al mixer (il titolo scrive PCM negli 8 blocchi
della porta 0, `AUDIO_PEAK=1` mostra l'anello pieno) e il dispositivo SDL e'
aperto: se non si sente, il problema e' a valle. Il `[park-watchdog]` da
7-11 s al caricamento del livello c'e' dalla s.37 in meta' delle corse
(io 0A400000/0A500000), i fotogrammi continuano, poi il livello parte:
benigno finche' non compare nel livello. Il processo tiene 13 GB di
texture Metal (`vmmap`: 164 x 32 MB + 5.847 x 1 MB, IOAccelerator 13,2 GB
residenti; cache=20.000 voci nel framestat): la cache delle texture non
libera abbastanza -- da guardare se la memoria diventa un problema.

### Fronte aperto (dopo la sessione 39)

1. Il triangolo gigante raro (punto 3).
2. La cache delle texture a 13 GB.
3. Velocita' del codice SPU liftato (unica leva rimasta per i fps): cache
   dei registri con analisi vivi/sporchi, u128 in ordine SPU.
4. Giocare oltre il checkpoint con la tastiera (Elia); audio: si sente?

## Sessione 38 — lane SPU indipendenti (12,6 -> 24-26,6 fps) e fetch dei vertici con piano per draw (-> 28 fps)

**Il collo era il modo in cui il nostro kernel SPURS distribuiva i quanti.**
Con `SPURS_LANE_STAT=1` nel livello (finestra da 2 s a 150 s, binario s.37,
12,6 fps mediani): 10.927 quanti del wid 12 (geometria), ognuno = sveglia
delle 5 lane del pool + join sulla piu' lenta, con le lane occupate al 69%
del quanto; il thread kernel al 90% del tempo dentro quanti, di cui il wid
12 solo il 47% -- l'altro 43% erano quanti degli altri workload, durante i
quali il wid 12 NON girava (e i workload a UNA SPU, wid 2/4/9/10, tenevano
ferme le altre 5 lane). Somma delle lane: 6,4 s ogni 2 s = 53% di 6 lane.
Sul ferro non c'e' niente di tutto questo: ogni SPU esegue il proprio ciclo
del kernel, sceglie un workload pronto con contention libera, ne esegue un
quanto, sceglie di nuovo.

**Fatto: `SPURS_LANE_SCHED=1` (`libs/spurs/cellSpurs.c`,
`spurs_lane_sched_thread`; default in `run.sh`).** N thread lane
(`SPURS_LANES`, default 6) girano da soli: ognuno scorre i workload a
rotazione (dal successivo all'ultimo eseguito, cosi' l'audio wid 7 e le
liste sincrone non aspettano la geometria), prende il primo abilitato con
lane libere (in corsa < maxContention, la stessa di prima: FORCE_SPUS solo
per i multi-SPU, le liste a una SPU restano su una lane), si assegna uno
spuNum con una maschera atomica ed esegue un quanto del PM. Un workload che
all'ultimo quanto non ha trovato lavoro (`polls==0`) viene ritentato dopo
50 us << streak (tetto 800 us); un kick lo azzera; lane senza nulla da fare:
`SPURS_LANE_IDLE_US` (100). `g_spurs_pm_polls/exited` ora per thread
(`SPU_THREAD_LOCAL`): con piu' corse in volo erano un globale condiviso.
`s_wkl_running[wid]` e' un contatore (shutdown wait invariato). Il vecchio
thread kernel resta il default nel codice (Windows e altri titoli):
`SPURS_LANE_SCHED=0` per l'A/B. `SPURS_KERN2_WIDS` non conta piu'.

**Due prove fallite prima di quella buona, tutte e due al caricamento (il
main thread girava per sempre e il livello non partiva):**
1. spuNum = il bit libero piu' basso: con quanti da 5 us tutti finivano su
   spuNum 0. Il job manager tiene una riga di ticket per SPU e la lista non
   "finisce" finche' ogni SPU non l'ha percorsa: il wid 6 kickato con
   readyCount=8 non chiudeva mai. Ora lo spuNum RUOTA (`next_sn`).
2. Il byte di contention dell'istanza (`SPURS_WKL_CURCONT`) veniva scritto
   all'inizio del quanto e mai azzerato: il titolo lo legge con
   `cellSpursGetWorkloadInfo(wid=6)` e aspetta che torni a 0 (`sample`: main
   thread in `func_001AD590` -> GetWorkloadInfo in ciclo). Ora vale il numero
   di lane in corsa e torna a 0 all'uscita.

**Misure (mediana 100-190 s, `fpsmed.sh`): 26,6 / 24,1 / 23,8 fps con 6
lane (3 corse), 25,5 con `SPURS_LANES=8`, contro 12,6 del binario s.37 nella
stessa corsa di riferimento.** 6.100 fotogrammi in 200 s contro 4.250. Il
livello si carica in 6 s invece di 15. Nessuno stallo in 5 corse (200/200/
150/135/113 s); un `[park-watchdog]` da 7,5 s durante il caricamento del
livello (fotogrammi che continuano, io=0A400000, poi il livello parte), da
capire se e' solo il caricamento. Immagine identica alla s.37
(`docs/img_s38_dive_boat_lane_sched.jpg`). Lane-stat a 150 s: wid 12
114.000 quanti in 2 s (un job per quanto, ~50 us), lane occupate all'82%.

**Dove va il tempo ORA (`sample` a 100 s, `GCM_TIMELINE`):** put -> walk
~40-54 ms, walk -> put 0,3 ms: il PPU aspetta il walker. Il ticker e' al
70%: 56% dentro `gcm_rsx_process_fifo` (di cui `draw_clause_flush` 47% del
thread: `record_draw` + `fetch_vp_vertex`/`rsx_fetch_attrib` = i vertici
copiati sulla CPU a ogni draw), 15% in `rsx_metal_backend_present`
(`newBufferWithBytes` = memmove), 29% a dormire (2,7 park per fotogramma,
erano 12). Le 8 lane non aiutano (25,5 fps): il prossimo collo e' il walker
stesso, cioe' il costo per draw del backend Metal, non piu' le SPU.

**Poi il walker: fetch dei vertici con un piano per draw -> 28,2-28,4 fps.**
`rsx_metal_backend.m`: per i draw con VP del guest, `vplan_build` calcola
una volta per draw base/stride/tipo/frequenza/default di ogni attributo
della maschera e `fetch_vp_vertex_fast` fa per vertice solo indirizzo
(base della pagina da 1 MB in cache, cosi' la IO table resta giusta) e
conversione; niente piu' `MtlVertex` (pos/col/tc: 3 fetch in piu' e 48 MB
di `newBufferWithBytes` a fotogramma) per quei draw; `resolve_indexed` con
la pagina degli indici in cache per draw. Provata l'equivalenza:
`RSX_VFETCH_CHECK=1` rifa' ogni fetch con la via lenta e confronta i byte:
**3,8 miliardi di fetch in 120 s, 0 differenze** (le divisioni restano
divisioni: `x*(1/255.f)` dava 1 ULP di differenza in 3% dei casi).
`RSX_VFETCH_FAST=0` per l'A/B. Ticker: da 70% occupato (39% nel fetch) a
53% (18%). Immagine giusta (`/tmp/s38_vf_110.png` = stessa scena).

**Idle per spuNum (`idle_mask`).** Il PPU aspetta un job con
`func_0074073C` = ticket <= min delle 6 righe di avanzamento per SPU della
lista (61% del main thread in quel poll): ogni spuNum deve vedere la fine
della lista. Il backoff del workload ora scatta solo quando TUTTI gli
spuNum hanno visto la lista vuota, e finche' non lo sono si scelgono per
primi quelli mancanti. Misurato neutro (28,2 contro 28,4), tenuto perche'
e' la semantica giusta.

**Ultime due corse del binario finale (con la snprintf per ogni PUT del
DMA tolta dal cammino caldo, 2% delle lane): 26,7 e 27,2 fps mediani, 200
s l'una, nessuno stallo.** Il rumore fra corse e' ~+-1 fps (morti di Drake
e stato del gioco diversi nella finestra): il numero della sessione e'
**27-28 fps** (26,7 / 27,2 / 27,8 / 28,2 / 28,4).

**Dove va il tempo ORA:** lane al 87% (wid 12: 129.000 quanti in 2 s da 52
us = 120 ms di lane per fotogramma, gli altri wid ~35 ms; 155 ms per
fotogramma contro i ~165 ms di budget SPU della PS3 a 30 fps: siamo vicini
alla parita' con il ferro), ticker 53%, PPU main per il 61% nel poll dei
ticket. `SPURS_LANES=8` PEGGIORA (22,5 fps: 10 thread pesanti su 10 P-core,
qualcuno finisce sugli E-core). Il titolo chiede `SetFlipMode(1)` = vsync:
sul ferro gira a 30 fps, quindi 28 e' di fatto il ritmo del gioco. Per
salire ancora serve che il codice SPU liftato sia PIU' VELOCE di una SPU
vera (cache dei registri con analisi di flusso, u128 in ordine SPU).

Chiuso: `RING_GUARD_LANES` 5 e' giusto -- con `GCM_RING_TRACE=1` le lane
dell'anello sono 0-4 (12.000 check/release l'una in 75 s), lo spuNum 5 non
e' una lane dell'anello (il job prende la lane dai propri dati, non dallo
spuNum).

### Fronte aperto (dopo la sessione 38)

1. Velocita' del codice SPU liftato (unica leva rimasta per i fps): cache
   dei registri con analisi vivi/sporchi, u128 in ordine SPU (`shufb` senza
   rev32), costo per quanto (`SPURS_PM_FRESH`: memset 16 KB + immagine).
2. Il present: `newBufferWithBytes` del flusso degli attributi (~80 MB a
   fotogramma, 6% del ticker): anello di 4 buffer `NoCopy` sulla memoria
   nostra (MTL_MAX_INFLIGHT 3).
3. Il park-watchdog da 7,5 s al caricamento del livello con le lane nuove.
4. Perche' chiudere il flip costa ~1 fps (s.37, punto 2); giocare oltre il
   checkpoint con la tastiera.

## Il ciclo di lavoro, automatizzato

`uncharted-data/iterate.sh` esegue il titolo, raccoglie gli indirizzi che il
runtime riporta come *unresolved indirect call*, li accumula in
`analysis/extra_targets.txt`, rilifta, ricostruisce e ripete. Si ferma quando un
giro non produce target nuovi: a quel punto il blocco non e' piu' un ingresso
mancante e serve guardarlo.

Ogni giro costa ~7 minuti di lift+build e non richiede decisioni finche' i
target continuano ad arrivare, quindi conviene lasciarlo girare.

**Quando i target si concentrano in una regione, non inseguirli uno alla volta.**
Passare in blocco tutti i blocchi raggiungibili solo per calcolo di quella
regione costa un giro invece di dieci:

| regione | cosa contiene | blocchi |
|---|---|---|
| `0x650000-0x680000` | FIOS, threading del titolo | 341 |
| `0xE0000-0xF0000` + `0x30000-0x40000` | PackageMgr e il loader di livelli | 108 |

## Cosa manca ancora al backend Metal

vertex program del guest in MSL; depth/stencil, MRT, render-to-texture; il
crossbar `TEXTURE_CONTROL1` e i sampler per unità.

Dalla sessione 21 la **modalità immediata** c'è (`draw_inline` nella vtable,
`SET_VERTEX_DATA2F/4F_M` fra BEGIN e END) — era lei a mancare, non i pezzi qui
sopra, e senza di lei al backend non arrivava un solo draw. Dalla sessione 22
ci sono anche il **render-to-texture** (una `MTLTexture` per superficie non
display, passaggi raggruppati per superficie), il **ritaglio** (scissor) e la
**decompressione BC1/BC2/BC3** sulla CPU (`rsx_texture_decode_bc`), più il
legame di tutti e 16 gli slot di texture che il fragment program del guest
pretende. Difetto noto del decompilatore dei fragment program: emette
`return r[0]` anche quando l'ultima scrittura è al banco a mezza precisione
`h[0]`, da cui dovrebbe venire l'uscita.

## Come si ricostruisce il port

```bash
~/Documents/Coding/uncharted-data/build.sh
cd ~/Documents/Coding/uncharted-data
PS3_HLE_INJECT_BASE=0x50000000 PS3_VFS_ROOT=/tmp/uc_iso \
    ./uncharted ~/Documents/Coding/uncharted-data/EBOOT.elf
```

La tabella NID è **per-port**: `ppu_hle_register_all()` nel runtime è weak e
vuota, e senza `tools/gen_hle_nids.py --all` ogni import resta non risolto.

**Dal 24/09/2026 la ricetta completa sta anche nel repo, in `uncharted/`**
(`prepare.sh` nuovo; `relift_spu.sh`, `build.sh`, `run.sh`, `runfor.sh`,
`iterate.sh` copiati da uncharted-data con i percorsi relativi al repo,
`UNCHARTED_DATA` per spostare la cartella dati). Verificato rigenerando in una
cartella a parte: analisi dell'ELF, ingressi indiretti, immagine SPU e policy
module (`wrap_spu_elf.py --entry 0xa00 --base 0xa00` sul dump
`spu_dump/spu_pm_1008EB00_12160.bin`) **identici byte per byte**; il lift PPU
da' le stesse 26.650 funzioni, corpi uguali tranne 2 al confine fra chunk
(func_001B156C, func_001B18DC: i commenti coi nomi spostano i confini).
`names.json` rigenerato ha 1.741 voci contro 1.772 (solo commenti). La tabella
NID rigenerata ha dichiarazioni in piu' rispetto a quella in uncharted-data
(cellGcm_gpu_idle, cellGcm_note_park_put, ...): quella di lavoro e' vecchia.
NON provati: `relift_spu.sh` e `build.sh` della copia nel repo (compilazione
pesante, c'era una serie di corse in parallelo). Gli script in uncharted-data
sono ancora gli originali: le due copie vanno tenute allineate o sostituite
da un rimando.

## Pulizia uncharted-data (23/09/2026)

Cartella `uncharted-data` 40 GB -> 1,0 GB. Cancellati: 930 `run_*.log` (37,5 GB,
s27-s39), `iterate.log`, `shot.log`, `uncharted.s37`, `recomp.bak`,
`recomp.bak_s35`, `recomp.bak_s36`, e in `obj/`: `o1_backup`, `o2_s35_backup`,
`o2_s36_backup`, `spu_s35_backup`, `spu_s36_neon`. I backup citati sopra (s35/s36)
non esistono piu': per tornare indietro si rifa' il lift da git.
## Calore: pausa nell'attesa del PPU (24/09/2026): -10% di CPU per fotogramma, -0,4 core

Misura di partenza con `scratchpad/misura/cpufoto.sh` (letta sulla corsa
s44ad_18, SPU_ASSERT_DMA=1, 30 s nel livello): **271 ms di CPU per
fotogramma, 6,52 core, 24,1 fps**. Gli fps sono al tetto del vsync, quindi
il numero da far scendere e' questo, non gli fps.

Il main thread aspetta i ticket dei job SPU girando in `func_0074232C`
(ciclo a `loc_00742388` che richiama `func_007428C0`): 61% del suo tempo
(s.42). Il rilevatore di spin in `vm_read32` non lo vede (ogni giro legge
indirizzi diversi). Nuovo: `ppu_lifter.py --spin-backoff 0x742388` mette
`ppu_spin_backoff()` sul salto all'indietro; in `ppu_loader.cpp` 64 giri a
vuoto, poi `sched_yield`, poi `nanosleep` 20-50 us; un'attesa nuova dopo
una pausa > 200 us. `PPU_SPIN_BACKOFF=0` lo spegne (A/B con lo STESSO
binario). Verificato senza gioco: il lift cambia solo `func_0074232C`
(26.650 funzioni confrontate), `ppu_loader.cpp` e il chunk compilano.
**Misurato (sera del 24/09, macchina libera)**, stesso binario, alternate,
`cpufoto.sh 90` (30 s nel livello), 4+4 corse:
- `PPU_SPIN_BACKOFF=0`: 241/249/263/235 ms CPU/fotogramma (media 247), core
  6,38-6,51, 26,2 fps; thread PPU 33-37 ms/fotogramma.
- con la pausa: 234/212/230/210 (media 222), core 5,96-6,08, 27,3 fps;
  thread PPU 18 ms/fotogramma.
Le due serie di core non si sovrappongono. Nessun HALT/assert nei log. Il
lift installato in uncharted-data/recomp ha la pausa (solo
ppu_recomp_005.cpp e ppu_recomp.h cambiati, una riga di codice).
`cpufoto.sh` ora stampa anche i ms CPU/fotogramma per thread (indice nella
lista di `ps -M`): 7 thread pesanti = PPU + 6 lane SPU (~30 ms ciascuno),
poi il ticker (~18). Profilo per thread (sample, 10 s nel livello): le lane
passano ~65% nel codice dei job, ~10% a interrogare nel policy module, ~21%
ferme; il ticker ha `getenv` nel cammino caldo (`__findenv_locked`, 8% del
thread). Prossimi bersagli per il calore: l'interrogazione del policy module
nelle lane e quel getenv.

**getenv nei cammini caldi tolto** (stessa sera): 12 siti che leggevano una
variabile d'ambiente a ogni chiamata (rsx_commands.c: RSX_REPORT_DBG,
LOAD_DBG, SEQ_DBG x2, MVPDBG; cellGcmSys.c: GCM_RATE, GCM_DRAINDBG,
GCM_SCANREF; spu_channels.c: SPU_ARGWATCH in spu_run_with_halt; ppu_hle.cpp
e lv2_register.c: YDKJ_GFXSCAN; ppu_loader.cpp: YDKJ_SCTRACE,
YDKJ_BLOCKTRACE) ora la leggono una volta (static, stesso pattern degli
altri). `sample` 10 s nel livello: campioni in getenv 350 -> 3 (~1,3% della
CPU del processo). Troppo poco per cpufoto (rumore +-10%): verificato solo col
profilo e con una corsa normale (fotogramma #3661 a 80 s, nessun assert).

**Azzeramento del policy module davvero parziale + orologio veloce** (stessa
sera). (1) Con `SPURS_PM_FRESH=1` ogni esecuzione del PM (~10.000/s) azzerava
256 KB: `spu_context_init` fa `memset` di tutto il contesto, e in
`spu_run_policy_module` veniva chiamata DOPO l'azzeramento parziale della s.28
(solo LS 0..0x4000), annullandolo -- dal primo giorno. Ora nel ramo parziale
si impostano a mano spu_id/status; `SPURS_PM_FRESH_LS=40000` ridà
l'azzeramento totale (A/B). (2) `ps3__mono_ns` (win32_compat.h) e
`spurs_ns_now` (cellSpurs.c) su macOS usano `clock_gettime_nsec_np(
CLOCK_UPTIME_RAW)`: CLOCK_MONOTONIC passa da `_mach_boottime_usec`.
Profilo 10 s nel livello: `__bzero` 730 -> 55 campioni, `clock_gettime`
218 -> 29 (~2,8% della CPU del processo). cpufoto A/B (4+4, solo il punto 1):
218 contro 232 ms/fotogramma, dentro il rumore. **Stabilita': lotto.sh 10
corse da 160 s, 10/10 OK, 0 assert del PM, fotogrammi a 160 s 6.087-6.399
(media 6.250; lotti di controllo s44ad/s44pk ~5.970/5.850).** La LS alta del
PM ora resta sporca fra una corsa e l'altra, come sul ferro: se riappare uno
stallo del PM, provare prima `SPURS_PM_FRESH_LS=40000`.

**Atomici SPU senza lock in lettura + decrementer senza `isb`** (sessione 45,
24/09 notte). Profilo per thread (`prof2.sh`, 10 s a 100 s, `callers.py` in
scratchpad di sessione) prima: `resv_lock_spin` 1.211 campioni su ~25.000
attivi (4,8%), di cui 715 dal job 0089df80 `func_000075F0` (allocatore ad
anello: GETLLAR / calcolo / PUTLLC / riprova, sei lane sulla STESSA riga) e
294 da 008ff700 `func_00004038`; `mach_absolute_time` 747 (3%) quasi tutto da
`spu_host_ns` <- `SPU_RdDec` (job 008dc900 e policy module). Modifiche, tutte
in `runtime/spu/spu_channels.c`:
1. seqlock sopra `g_resv_lock` (`g_resv_seq`, dispari mentre qualcuno scrive
   sotto il lock): GETLLAR copia la riga senza lock e riprova se la sequenza
   cambia (`resv_read_line`); PUTLLC fallisce SENZA lock se la riga e' gia'
   diversa dall'istantanea (prenotazione persa comunque); commit del PUTLLC
   confrontato a 8 byte per volta (meno tempo sotto il lock);
2. `spu_host_ns` su macOS/arm64 legge `cntvct_el0` direttamente (stessa unita'
   di mach_absolute_time, senza `isb`);
3. `atomic_fetch_add(&g_spu_rddec_count)` a ogni `rdch Dec` solo con
   `SPU_LABEL_POLL` (era una riga condivisa da sei lane).
A/B con lo stesso binario: `SPU_ATOMIC_LOCKALL=1 SPU_HOST_NS_MACH=1` = prima
(il punto 3 non ha interruttore). Profilo dopo: `resv_lock_spin` 1.211 -> 638,
`mach_absolute_time` 747 -> 176 (resta `spurs_lane_sched_thread`),
`spu_rdch` 390 -> 172.
**cpufoto, 10+10 corse alternate**: la CPU per fotogramma segue il CONTENUTO
(~6 ms ogni 100 draw medi; corse con Drake altrove danno 850 draw e 120 ms):
confrontate con una retta comune CPU~draw, **-9,9 +- 5,3 ms/fotogramma (~5%)
a parita' di draw** (a 2.000 draw: 197 -> 187). `cpufoto.sh` ora stampa anche
i draw medi della finestra: confrontare solo a draw simili.
Stabilita': lotto alternato 6+6 da 160 s tutto OK; nelle 20 cpufoto un fermo
per parte (c45on_3 e c45off_6, tipo B senza assert: gia' noti, ~1 su 10). In
una corsa (c45ab_3_v1, con le modifiche) il gioco e' rimasto nel menu dopo
l'assert `joblist.cpp(524): IsListFinished()` -- NON nuova: compare nel 22%
delle corse s.43-44 (60 log), 6 volte con la corsa ferma nel menu.
Prossimi bersagli: la contesa vera sulla riga dell'allocatore di 0089df80
(638 campioni restano, sono PUTLLC in conflitto), `spurs_lane_sched_thread`
che legge l'orologio (143 campioni), `mfc_for` che prende il lock per il PM
a ogni esecuzione (contesto rinfrescato: mfc_cache azzerato).

**Lock degli atomici diviso per riga (64 strisce)** (sessione 45, dopo).
Nuova diagnostica `SPU_ATOM_STAT=1` (conteggi per riga ogni 5 s: GETLLAR /
PUTLLC riusciti / falliti senza lock / falliti sotto lock). Nel livello: la
riga 0x10184B00 (img 2) fa ~142.000 GETLLAR/s, 114.000 PUTLLC riusciti/s,
~22.000 falliti sotto il lock/s; altre 4-5 righe (0x106C8F00, 0x106AFE80,
0x10696E80, 0x1057BF80) ~50.000/s ciascuna. Con un lock unico ognuna
aspettava anche le altre. Ora `g_resv[64]` (lock + sequenza, 128 byte
ciascuno, indice (ea>>7)&63); `spu_atomic_line_lock` (HLE PPU, senza
indirizzo) le prende tutte in ordine (non compare nel profilo); `mfc_for` usa
la striscia 0. Tolto anche `g_spu_putllc_count++` (nessuno lo legge, riga
condivisa da sei lane). A/B: `SPU_ATOMIC_STRIPES=1` = un lock solo.
Profilo (10 s a 100 s): `resv_lock_spin` 638 -> 238 campioni, `spu_wrch`
1.124 -> 504, con una scena piu' pesante (3.200 draw/fotogramma).
cpufoto 8+8 alternate, retta comune CPU~draw: **-5,4 +- 5,4 ms/fotogramma,
dentro il rumore** (pendenza 4,8 ms ogni 100 draw). Stabilita': lotto
alternato 7 corse da 160 s (3 nuove + 4 `STRIPES=1`, l'ottava interrotta dal
riavvio del Mac) tutte OK. I 224 campioni di attesa che restano sono la
contesa vera sulla riga 0x10184B00 (job 0089df80 `func_000075F0`).

**Trasferimenti SPU e salti indiretti** (sessione 46, 26/09). Profilo (10 s a
100 s) prima: ~25.000 campioni attivi, di cui `spu_drain_fast` 1.279,
`spu_indirect_branch` 889, `mfc_do_transfer` 816, `mfc_channel_write` 698,
`spu_lookup` 550, `spu_wrch` 515. Tre modifiche:
1. `mfc_do_transfer` (spu_dma.h) ha una via veloce: senza variabili
   diagnostiche fa solo validazione, memcpy e gli effetti veri (note
   dell'overlay, report RSX in ritardo, park dei pacchetti, flag cri). Il
   percorso completo passava ~35 sonde e il rilevatore POISON-DMA scandiva
   parola per parola OGNI PUT. Malformati/fuori memoria scendono nel percorso
   completo. A/B: `SPU_DMA_FULL=1`. Campioni 816 -> 110-130.
2. cache per contesto dei salti indiretti (`ib_cache[256]` in coda a
   spu_context, chiave pc|immagine<<32|overlay<<48): il registro e' riempito
   solo all'avvio (boot_main -> spu_register_all), quindi non scade mai.
   A/B: `SPU_IB_NOCACHE=1`. `spu_lookup` 550 -> ~190 (1.024 voci = stesso
   numero: i residui non sono conflitti). `spu_indirect_branch` in se' resta
   ~850: e' il salto indiretto stesso, come `spu_drain_fast` (~1.200, un salto
   imprevedibile per hop). Per scendere ancora servono meno hop (lifter), non
   un runtime piu' magro.
3. in `mfc_submit` il contatore della traccia del PM (`_pn`) si incrementava a
   ogni DMA del PM da sei lane (riga contesa): ora oltre il tetto e' solo
   letto. `mfc_channel_write` 652 -> ~380-410.
**cpufoto 7+7 alternate** (le prime due modifiche spente con le variabili, la
terza non ha interruttore), retta comune CPU~draw: **-5,0 +- 4,1 ms/fotogramma**
(a 2.200 draw: 180 -> 175), pendenza 4,1 ms ogni 100 draw. Una corsa con le
modifiche (c46_1_on) ferma in silenzio a #2965 senza assert (tipo B noto).
**Lotto 8 corse da 160 s: 8/8 OK, 0 assert, fotogrammi 6.523-6.660 (media
~6.600; lotto di controllo della sera del 24/09: 6.250).**
Build: il runtime va ricompilato con `cmake --build build` PRIMA di
`uncharted/build.sh` (la prima misura era sul runtime vecchio: 0 effetto).
Restano in cima: job 008dc900 (codice del gioco), fetch_vp_vertex_fast,
memmove, spu_wrch (atomici), mfc_for (~200).

**Draw indicizzati nel backend Metal** (sessione 46, dopo). `memmove` NON e'
del walker: ~85% sono le copie DMA delle SPU (lavoro vero), il walker ne fa
~40 campioni. Il bersaglio era `fetch_vp_vertex_fast` (~1.150 campioni):
sonda temporanea nel livello, 100 s: tutti i draw sono indicizzati (84%
triangoli, 16% quad), 2,26 miliardi di indici per 0,89 miliardi di vertici
diversi (2,5 letture per vertice; l'intervallo min..max sarebbe 1,67
miliardi, quindi leggere l'intervallo renderebbe poco). Ora (via veloce,
indicizzati) ogni vertice si legge UNA volta per draw (cache a mappa diretta
di 4.096 voci invalidata dal numero del draw, `s_vc`), gli indici (u32,
relativi a vbase) vanno in un terzo anello NoCopy (`s_vidx_ring`, 32M
indici/fotogramma) e la codifica usa `drawIndexedPrimitives`. Spento con
`RSX_VIDX=0` (A/B) e da solo con le sonde che rileggono s_vpd in ordine di
vertice (RSX_VA_WATCH/GARBAGE/GIANT/VP_DBG/VPD_CHECK). Screenshot a 110 s:
scena corretta.
Profilo: `fetch_vp_vertex_fast` 1.146 -> ~480, `record_draw` 97 -> ~330 (la
cache nel PUSH; compattarla in una struct non cambia nulla): netto ~-400.
cpufoto 6+6 alternate: **-6,9 +- 5,0 ms/fotogramma** a parita' di draw
(esclusa cvx_2_on, ferma).
**Stabilita' (dubbio aperto)**: fermi tipo B (PPU in `func_00742838`/
`func_0074073C` che aspetta la job list, walker innocente) con RSX_VIDX
acceso 4 su 22 corse (cvx_2_on, l46vx_4, l46vx_7, abvx_4_v1), spento 0 su
14 (cvx off, abvx v0). Fisher p = 0,12: non significativo, frequenza storica
~13% (12 su 92 nei lotti s.44). Anche prima degli indici, nella s.46, un
fermo (c46_1_on) su 15 corse. Se i fermi sembrano piu' frequenti,
`RSX_VIDX=0` e' la prima prova; un lotto 20+20 alternato lo decide.
**Lotto 20+20 alternato (ab20, 160 s, framecatch)**: RSX_VIDX=1 1 fermo su
20 (ab20_12_v1 a #4042, stesso tipo B: PPU in func_00742838), RSX_VIDX=0 0 su
20; fotogrammi medi delle corse OK 6.572 contro 6.460. Contando solo i lotti
alternati (abvx + ab20): 2/28 contro 0/28 (p ~ 0,25), sotto la frequenza
storica del tipo B (~13%). Gli indici restano accesi.

**Operazioni SPU per corsia in NEON** (sessione 47, 26/09). Profilo per
indirizzo (xctrace Time Profiler, 8 s a 100 s; `leaf.py` nello scratchpad
della sessione): `jobmod_008dc900..._spu_func_00010300` = 7.366 su 45.662
campioni in esecuzione (16%), profilo PIATTO dentro la funzione (blocco da
64 byte piu' caldo 2%). Il disassemblato mostrava perche': i cicli
`for(i<4) r._u32[i]=a._u32[i]+b._u32[i]` di spu_a/ai/andi/... diventavano
aritmetica scalare a coppie di parole su registri x con maschere
0xffffffff00000000 (1.198 add e 992 and scalari su 15.690 istruzioni), e
spu_rotqbyi era ancora un ciclo di byte (chiamato fuori linea). Nuovo
`runtime/spu/spu_helpers_neon_arith.h`, incluso in fondo a spu_helpers.h:
55 operazioni (a/sf/ah/ai/sfi, logiche, selb, confronti con immediato,
scorrimenti/rotazioni con immediato, rotqbyi, mpy*, xshw/xsbh) in NEON e
sostituite con macro omonime (le scalari restano: `(spu_a)(x,y)` le chiama).
Spento con `SPU_DEFS="-DSPU_LS_NODIAG -DSPU_NEON_ARITH_OFF" ./uncharted/build.sh`.
Verifica: `tests/spu_neon_arith_diff.c` 11,6 M confronti, 0 differenze;
`tests/spu_helpers_audit.c` identico con e senza (le sue 1.710 differenze
su cflts/cfltu sono preesistenti). Funzione calda 15.690 -> 13.279
istruzioni. Screenshot a 110 s corretto.
**A/B di due eseguibili 7+7 alternate** (`scratchpad/misura/abbin.sh`, 160 s,
retta comune con `abfit.py`): **-9,0 +- 3,1 ms CPU/fotogramma** (a 2.200 draw
167 -> 158), 14/14 corse OK, 0 assert, ~31 fps. Restano: spu_shufb non in
linea in 39 siti della funzione calda, spu_fma_slow (~320 campioni: la via
esatta scatta davvero, probabilmente le sentinelle della tavolozza),
ls_read/write con rev32 (~500 per funzione: cambiare rappresentazione u128).

**spu_shufb sempre in linea: NESSUN effetto, tolto** (s.47, dopo). Con
`__attribute__((always_inline))` spariscono le 751 chiamate fuori linea di
spu_shufb dal binario (39 nella funzione calda, che passa 13.279 -> 13.743
istruzioni), ma l'A/B 7+7 (c47s) da' **+1,4 +- 3,6 ms/fotogramma** (13 corse;
esclusa c47s_5_off, fermo tipo B a #3829 nella versione SENZA la modifica).
La chiamata non era il costo: non riprovare. Il fermo conferma che il tipo B
non dipende dalle modifiche di questa sessione.

**Local store in ordine di parola dell'host** (s.47, 27/09). Sonda: due rev32
in piu' per accesso lqd/stqd (con barriere asm) = +16,1 +- 3,7 ms/fotogramma,
quindi la rev32 esistente valeva fino a ~8 ms. Ora `SPU_LS_XOR` (nuovo
`runtime/spu/spu_ls_order.h`, default 3 su aarch64+NEON, 0 altrove): il byte
SPU `a` sta all'indice fisico `a ^ 3`, i quadword si leggono con una load
semplice (rev32 nella funzione calda 518 -> 3, istruzioni 13.279 -> 12.699).
Tutti i ~130 accessi del runtime alla LS passano da accessori in
spu_context.h: `spu_ls_get8/set8/get32/put32`, `spu_ls_copy_in/out` (DMA,
atomici, caricatori ELF/job, contesto del PM), `spu_ls_zero`, `spu_ls_be`
(vista big-endian in un buffer del thread per sonde e dump). Toccati anche
lv2_register.c (sys_spu_thread_read/write_ls, caricamento immagini) e
libs/spurs/spurs_pm.c. Regola: nessuno tocca `ctx->ls[...]` o fa memcpy
da/verso la LS se non attraverso questi. A/B: runtime e SPU con
-DSPU_LS_XOR=0 (`cmake -B build_x0 -DCMAKE_C_FLAGS=-DSPU_LS_XOR=0 ...`,
`SPU_DEFS="-DSPU_LS_NODIAG -DSPU_LS_XOR=0" RT_BUILD=build_x0 ./uncharted/build.sh`).
Tre lotti 7+7 (c47x/y/z), 32 corse a regime (>= 30,5 fps): **-3,6 +- 1,6
ms/fotogramma** (159 -> 155 a 2.200 draw). Meno della sonda: le barriere asm
gonfiavano il costo. Fermi (fps 2-10) in entrambe le versioni: 5 col formato
vecchio, 3-4 col nuovo, su 42 corse -- frequenza alta dopo il riavvio del Mac
(lotto c47y: 6/14), non legata alla modifica. Screenshot a 110 s corretti.

## Storico

Sessioni fino alla 37 e la vecchia "Dove siamo", verbatim, in
`docs/STATO_STORICO.md`:

- Dove siamo
- Sessione 5 — il SIGBUS è risolto
- Sessione 6 — due dei tre blocchi chiusi
- Sessione 18 — il job manager carica davvero, e la correzione della 17
- Sessione 18b — SBLOCCATO: il primo job WwsJob gira davvero
- Sessione 37 — cellGcmSetWaitFlip non blocca piu' il PPU: 10,7-11,9 -> 13-14,5 fps; via in linea degli accessi PPU (neutra)
- Sessione 36 — barriere di memoria nel lifter PPU; ShutdownWorkload/WaitForWorkloadShutdown veri
- Sessione 35 — lo stallo alla morte di Drake era il wid 3 (lista a UNA SPU) girato su 6 lane
- Sessione 34 — la tonalita' e' deterministica: il fotogramma "slavato" era l'effetto danno del gioco
- Sessione 33 — braccia e gambe di Drake: la SPU non ha i NaN
- Sessione 32 — il fantasma di Drake era la WPOS: i materiali leggevano i buffer di luce con x raddoppiata
- Sessione 31 — l'oracolo RPCS3 gira sul Mac; cubemap e probe RGBA16F: il personaggio ha il suo materiale
- Sessione 30 — il livello come nel gioco: draw coalescenti, maschera di colore, crash del present
- Sessione 29 — il livello a schermo: costanti FP, NV0039, texture in streaming, normali CMP
- Sessione 28 — l'assert della job list audio era il nostro kernel SPURS: r4 con il layout sbagliato
- Sessione 27 — il main loop non disegnava: CR6 mai scritto dai confronti vettoriali
- Sessione 26 — dal caricamento al menu principale: il gioco aspettava un tasto
- Sessione 25 — il walker era innocente: due thread scrivevano nello stesso command buffer, per un salto mal liftato
- Sessione 23 — lo stallo intermittente erano due tabelle da 8 slot nel runtime SPU
- Sessione 22 — la prima immagine vera: tre difetti in fila fra i vertici e lo schermo
- Sessione 21 — perche' lo schermo era nero: il titolo disegna in modalita' immediata
- Sessione 20 — lo stallo dopo il caricamento era il job manager, non l'event flag
- Sessione 19 — il segmento tagliato sotto i piedi
- Sessione 18d — e ora non aspetta piu' nemmeno la RSX
- Sessione 18c — il PPU non aspetta piu' la SPU: ora aspetta la RSX
- Sessione 17 — il segnale mancava sul serio, ma non dove lo cercavo
- Sessione 16 — CORREZIONE: non manca nessun segnale
- Sessione 15 — la diagnosi sbagliata, tenuta come riferimento
- Sessione 14 — il titolo legge i propri dati
- Sessione 13 — il primo frame, e il file system del titolo
- Sessione 12 — il gioco carica i livelli
- Sessione 11 — chi scrive LS 0x12A0 / 0x12C0, e dove sta davvero il PPU
- Sessione 10 — il policy module gira davvero
- Sessione 9 — SBLOCCATO: il kernel SPURS gira
- Sessione 8 — cosa aspettava il gioco
- Da qui
- Storico del blocco 3 Il gioco ora arriva più avanti e si lamenta da solo:
- I tre blocchi originali (storico)
- Il blocco storico (risolto, tenuto come riferimento)

