# Stato del progetto

Ultimo aggiornamento: **8 settembre 2026** — sessione 18.

## Dove siamo

**Uncharted: Drake's Fortune esegue su macOS.** Non è giocabile, ma dalla
sessione 17 il canale di lavoro PPU→SPU funziona davvero: il titolo carica i
pacchetti dei livelli, spinge job nella propria coda lock-free, ogni push sveglia
il task SPU che li consuma, e arriva a eseguire il proprio codice di HUD. Il
percorso completo — ELF decifrato → lift → compilazione nativa ARM64 →
esecuzione — funziona.

Quanto arriva a fare, dal log:

- supera `sys_initialize_tls` (dove il boot upstream si fermava);
- inizializza il proprio TTY, alloca le strutture globali (i log portano i nomi
  dei sorgenti originali: `game/hashtable.inl`, `game/textdb.cpp`, `game/big.cpp`);
- stampa il proprio banner: **"Compiled on Oct 19 2007 at 10:32:52"**;
- configura il video a **1280x720**, trova `/dev_bdvd/PS3_GAME/USRDIR`;
- esegue `BigInit()`;
- **inizializza SPURS** (due istanze, 5 SPU + 1) e crea i thread
  "Job Manager Event Handler" con le loro event queue;
- **zero import firmware non risolti** su 172.

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

## Sessione 5 — il SIGBUS è risolto

**Causa:** `ppu_set_stack` lasciava 48 byte sopra sp. Un corpo di thread è
compilato come una funzione qualsiasi e scrive nel frame che il suo *chiamante*
dovrebbe possedere — e un thread non ha chiamante. Il Job Manager di Uncharted
scriveva a sp+0x30, poi (alzando la riserva) sp+0x80, poi sp+0x200: sempre un
byte oltre la fine dello stack, dentro la guard page del blocco successivo.
Ora la riserva è 4 KB. Windows non lo vedeva perché lì le pagine guest si
committano su richiesta invece di essere PROT_NONE.

**Lo strumento che l'ha trovato:** la trap POSIX (`ps3_install_guest_ptr_trap`,
prima uno stub vuoto che *nessun port chiamava*). Classifica il fault, traduce
l'indirizzo host in EA guest e stampa cia/lr/sp e la funzione responsabile.

**Dove arriva ora** (con `PS3RECOMP_METAL_HEADLESS=1`, 3195 righe di log, nessun
crash — si ferma in attesa):

- i due thread del Job Manager attendono sulle proprie event queue;
- SPURS: `GetInfo`, **`AddWorkload(wid=0, size=12160)`** — carica il programma SPU;
- `[SPU] initialize(nspu=6)`;
- carica i moduli: SPURS, IO, AUDIO, FS, NET, NETCTL, LV2DBG;
- **manda i primi comandi RSX**, e lì si ferma: `FIFO JUMP to unmapped IO
  0x1E8101C0`, poi uno spin loop (`HOTREAD spinning on 0x00000000`).

## Sessione 6 — due dei tre blocchi chiusi

**1. NSWindow — RISOLTO.** AppKit vuole il main thread; il backend creava la
finestra da un thread guest. Ora `create_window` fa hop sulla main queue, il
pump degli eventi è un no-op fuori dal main thread, e su Apple `boot_main`
esegue il guest su un pthread dedicato (stack host da 64 MB) lasciando il main
thread nel suo run loop — è questo che rende il `dispatch_sync` sicuro invece di
un deadlock. Uncharted apre la sua finestra: *windowed on Apple M4 Pro (1280x720)*.

**2. FIFO JUMP — RISOLTO.** La finestra HLE (control register, label, offset
table) stava al default `0x20000000`, cioè dentro la memoria di Uncharted (IO a
`0x21000000`, heap fino a ~`0x2B200000`). Il gioco sovrascriveva il control
register: `put` si rileggeva come `0x080018B7`, nemmeno allineato a 4, e il
walker decodificava dati vertex come comandi (`0x3E8101C0` è un float).
`$PS3_HLE_INJECT_BASE` la sposta senza ricompilare; per Uncharted **0x50000000**.

**3. I puntatori corrotti — RISOLTO, e non era "Rend".**

Il messaggio `Mapped mesh output buffer of 8MB at 52656e64` era una conseguenza.
Tre righe più sopra nel log c'era la causa: `unresolved indirect call ->
0x00705A78`. Il gioco chiamava attraverso un puntatore, il dispatch non trovava
nulla, la chiamata non avveniva, il valore di ritorno restava quello che il
registro conteneva — e il titolo proseguiva con spazzatura.

Quell'indirizzo cade *dentro* `func_007058E8` (+0x190), su una tabella di
ingressi alternativi che il compilatore mette in coda alla funzione:

    li  r11, <n>
    b   <corpo condiviso>

Il passo mid-function del lifter genera wrapper solo per indirizzi a cui
qualche istruzione **salta**; questi vivono nei dati e sono solo caricati in un
registro, quindi restano invisibili. `tools/find_indirect_entries.py` li estrae
(risalendo anche all'inizio della voce, che può precaricare r8/r6) e li unisce
al functions.json: **501 in BCES00065**.

Dopo il re-lift con quegli ingressi: **zero chiamate indirette non risolte**, e
il log è pulito —

    Trying to map 143MB of main memory IO space.
    Mapped IO size of 143MB at 22300000 to offset 01300000,
    Mapped SPU Job Memory of 4MB @20300000 to offset 0a200000
    [cellVideoOut] Configure: resId=2, format=0, aspect=0, pitch=5120
    [cellGcmSys] SetTile(index=0, loc=0, offset=0x50000, size=0x780000, pitch=5120)
    [rsx] backend init OK

Le mappature che prima rispondevano `FFFFFFFF` ora riescono, il video è
configurato a 1280x720 (pitch 5120 = 1280x4) e il backend RSX è su.

## Sessione 18 — il job manager carica davvero, e la correzione della 17

### Prima la correzione

Alla fine della sessione 17 avevo scritto che il job manager legge una job list
popolata **ma non stagia mai niente** — «nessun DMA arriva mai a LS 0x4000».
**Era falso, ed era falso per il modo in cui l'avevo misurato**: `SPU_DMATRACE`
si ferma a 160 righe, e il policy module fa centinaia di trasferimenti di
servizio a SPURS prima del primo che riguarda un job. Guardavo solo il rumore.

Con un filtro per *indirizzo* invece che per immagine (`SPU_DMA_EAWIN`) il
quadro cambia del tutto:

```
[dma-eawin] img=2 pc=0x03300 GET lsa=0x00C00 ea=0x108E0900 size=0x90
[dma-eawin] img=2 pc=0x03300 GET lsa=0x00C00 ea=0x108F0580 size=0x90
... otto, uno per job header ...
```

Il manager **prende davvero i comandi di caricamento** di tutti e otto i job,
e per ognuno completa il ciclo stall/riprendi e riserva i suoi buffer set:

```
[mfc-list] STALL  img=2 tag=0 elem@0x014F0 left=1 (SN raised, parked mask=0x1)
[spu-int]  TAKEN  img=2 events=0x2&0x2 vector->0x00A2C (srr0=0x02E28)
[mfc-list] RESUME img=2 tag=0 elem@0x014F8 left=1
[wws-res]  bufferSetArray read jobNum=0 logBufSet=0 ... 15
```

Otto volte, jobNum 0..7. La pipeline PPU→SPURS→WwsJob funziona molto piu'
avanti di quanto la 17 dicesse.

### Dov'e' il blocco vero

Al **nono** job la sequenza si rompe: lo STALL c'e', **l'interrupt no**, e
quindi nemmeno il RESUME.

```
[mfc-list] STALL img=2 ... loadJobState@12A0=00600001 2B153C80   <- nessun TAKEN
[mfc-list] STALL img=2 ... loadJobState@12A0=00900001 108E0900   <- nessun TAKEN
```

Da li' in poi il manager resta fermo nella sua attesa. Il contatore di stato lo
conferma, e stavolta e' un contatore vero e non un campione:

```
297 wid=7 halt_pc=0x02D68 loadState=1->1 loadJob=0x0 runJob=0xFFFFFFFF
 93 wid=4 halt_pc=0x02D68 loadState=1->1 loadJob=0x0 runJob=0xFFFFFFFF
```

`loadState` resta 1 (kReadCommands) per sempre, `runJob` non viene mai
assegnato: **nessun job arriva mai a girare.**

### Come e' fatto il pezzo di codice che aspetta

Vale la pena scriverlo, perche' ci si torna. Nel policy module (LS 0xA00-0x3980):

- **0x2D50-0x2D84** e' il loop di scheduling. Legge `loadState` da LS 0x12A0 e
  smista: 0 = niente da fare, 2 = interpreta i comandi (salta a 0x2EF8),
  3 = altro. Con `loadState == 1` cade a 0x2E20, che apre una **finestra di
  interrupt di una sola istruzione** (`ila $r2,0x2E28 / bie $r2 / ai $r0,$r0,0 /
  bid $r0`) e torna a 0x2D58. E' li' che gira.
- **0x3250-0x3324** prende i comandi di caricamento: `lqa $r2, 0x1420` e' il job
  header a 64 bit (halfword alta della prima parola = dimensione, seconda parola
  = EA), controlla `16 <= size <= 256` e fa il GET a **LS 0xC00**.
- **0x1BB0-0x1C1C** e' l'interprete dei comandi: legge quadword da LS 0xC00,
  avanza di 8 byte, e smista con `((cmd >> 9) & 7) * 4` sulla **tabella a LS
  0x1590** (8 voci: 0x2418, 0x1C20, **0x1D20** = il caricatore, 0x22B0, 0x2368,
  **0x23BC** = RunJob, 0x2418, 0x1604). Il salto vero e' il `bi $r6` a 0x2FB4.
- **0x2468-0x2528** e' la routine di interrupt (vettore a 0xA2C). Legge ch25
  (`MFC_RdListStallStat`), fa l'ack di ogni bit, e **solo se** `loadState == 1`
  **e** il bit 0 e' nella maschera scrive `loadState = 2` a LS 0x12A0.
- **0x15F0-0x1668** e' una **banca di `stopd`**, uno per assert. Il gestore di
  job di WwsJob e' pieno di controlli e ognuno finisce qui.

### Due cose che non si vedevano, e ora si vedono

1. **`stopd` era muto.** `spu_stop()` non stampa niente e ritorna: un'assert
   scattata era indistinguibile da un giro andato bene, e il modulo veniva
   semplicemente rieseguito al tick dopo. Ora il lifter passa l'indirizzo
   (`spu_stop_at`) e il runtime stampa `[spu-assert] img=2 stopd @0x015F8`, una
   volta per indirizzo. Quale controllo e' fallito e' meta' della diagnosi.
2. **Le diagnostiche DMA erano cieche per costruzione.** `SPU_DMA_EAWIN=lo:hi`
   filtra per indirizzo di memoria principale invece che per immagine, e
   `SPU_DMATRACE_MAX` alza il tetto di 160 righe che mi aveva fatto sbagliare
   la conclusione della 17.

## Sessione 18b — SBLOCCATO: il primo job WwsJob gira davvero

### La causa

L'interprete dei comandi di caricamento eseguiva **un solo comando per job**.
Misurato, non dedotto: `SPU_WWS_CMDS=1` traccia l'indice di ogni comando
smistato, e in tutta l'esecuzione ne uscivano otto, tutti indice 1.

Il perche' e' un mislift, ed e' la stessa classe che aveva morso su LBP, solo
dall'altro lato. I gestori tornano all'interprete cosi':

```
001D18  ila $r6, 0x1BDC      ; 0x1BDC = "prendi il comando successivo"
001D1C  br  0x2A88           ; blocco condiviso (una copia/azzeramento)
...
002AD4  biz $r4, $r6         ; le tre uscite del blocco condiviso
002AF4  biz $r75, $r6
002B14  bi  $r6
```

Ma **r6 e' anche un registro di link** nello stesso modulo (`brsl $r6, 0x2F38`,
`brsl $r6, 0x2FB8`, `brsl $r6, 0x2A88`), quindi `compute_link_returns()` le
classificava come ritorni di funzione ed emetteva `SPU_RET`. Invece di
continuare la lista, il gestore **usciva dall'interprete**. Il raffinamento sui
predecessori che il lifter gia' ha guarda solo il blocco che contiene il `bi`;
qui il registro viene calcolato dal chiamante, due blocchi piu' su.

Conseguenza a valle: il codice del job non veniva mai caricato, e il manager
falliva l'assert del magic `0xC0DEC0DE` a 0x33FC (`stopd` a 0x15B0) — che fino
a questa sessione era muto.

### La correzione

Nel lift del policy module (`uncharted-data/relift_spu.sh`):

```
--extra-funcs 0xA2C,0x1BDC,0x30E4
--force-indirect 0x2AD4,0x2AF4,0x2B14
```
piu' `SPU_RETLINK=1` in `run.sh` (vedi sotto).

`--force-indirect` rende le tre uscite dei salti calcolati. 0x1BDC e 0x30E4
vanno registrati come ingressi perche' `spu_indirect_branch` sa saltare solo a
un ingresso registrato: 0x30E4 e' l'indirizzo di ritorno di
`brsl $r6, 0x2F38`, e senza di lui il ritorno cadeva nel fallback a microstep
(`[spu-smc] pc=0x030E4 UNKNOWN word 0x1006321B`).

### SPU_RETLINK: il ritorno di un `bi $rN` che non e' r0

Registrare 0x30E4 apre un problema nuovo. Il blocco 0x2F38 e' chiamato con
`brsl $r6, 0x2F38`, e la sua uscita `bi $r6` ora e' un dispatch: dentro il
`SPU_DRAIN` della chiamata il dispatch salta gia' a 0x30E4, e poi il chiamante
ci cade dentro un'altra volta. **Il blocco veniva eseguito due volte**, e la
seconda passa -- con i registri gia' consumati dalla prima -- faceva scattare
l'assert 0x15D8.

La soluzione e' un controllo esatto, non un'euristica: il lifter registra in
`ctx->ret_link` l'indirizzo di ritorno della chiamata in corso (salvato e
ripristinato per annidamento), e `SPU_DRAIN` tratta come ritorno un
trasferimento il cui target E' quell'indirizzo. Diverso dalla scorciatoia
`SPU_TAILRET`, che confronta con `gpr[0]` e quindi indovina. Dietro
`SPU_RETLINK` perche' cambia il dispatch di tutti i titoli; acceso in `run.sh`
per questo port.

Tre serie da tre corse, stesso binario a parte i flag:

| configurazione | comandi | overlay | microstep | assert |
|---|---|---|---|---|
| prima della correzione | 8, tutti indice 1 | 0 | 0 | 0x15B0 |
| `0xA2C,0x1BDC` | 14 in 2 corse su 3 | 1 | 5 | 0 |
| `+0x30E4` | 14 in 3 su 3 | 1 | 0 | **0x15D8** |
| `+0x30E4` e `SPU_RETLINK=1` | **14 in 3 su 3** | 1 | 0 | **0** |

**E una trappola misurata.** Ci sono altri tre ritorni della stessa forma
(0x1960, 0x3030, 0x3610) e registrarli sembrava ovvio. Con quel set piu' ampio
l'interrupt di stall-and-notify **sparisce** e la pipeline non parte: tre corse
su tre con zero comandi. Registrare un ingresso spezza la funzione che lo
contiene e trasforma dei `goto` interni in trasferimenti via trampolino. Quindi:
si registra solo cio' che una misura chiede, uno alla volta.

### Il risultato

La lista dei comandi ora viene percorsa fino in fondo — sei coppie
(prenota buffer set, carica) e poi RunJob:

```
[wws-cmd] #1 idx=1 -> 0x1C20     #2 idx=2 -> 0x1D20   (x6)
[wws-cmd] #13 idx=3 -> 0x22B0
[wws-cmd] #14 idx=5 -> 0x23BC    <- RunJob
[wws-code] codeLS=0x00EB0 loadJobNum=0x0 nextLoad=0x1
[spu-ovl] img=2 streamed overlay src=0x10171CC0 -> LS 0x04000 size=16384
          -> resident ovl image 227 (sig match)
[dma-eawin] img=2 PUT lsa=0x3A000 ea=0x108DC900 size=0x2000
```

**Un modulo di job WwsJob e' residente in local store e ha riscritto 8 KB di
risultati in memoria principale.** Non era mai successo in nessuna sessione.
Riproducibile: **tre corse su tre**, identiche -- 14 comandi, un overlay
residente, zero microstep, cinque stall e due interrupt per corsa.

### Il codice del job gira

`SPU_JOBEXEC=1` lo conferma: sedici dispatch dentro l'overlay 227, con
`runJobNum=0`. Non e' piu' una questione di caricamento.

### E lo spin del PPU e' lo stesso fronte

Le 89.852 righe `[HOTREAD] spinning on 0x20000000` (su 104.710 totali) non sono
un problema separato. L'indirizzo di ritorno diceva `lr=0x00532620`, e la
funzione chiamata li' e' `func_00566108`:

```c
r11 = [r3 + 0x320];              // il puntatore al flag
loc_00566120:
    r9 = [r11];
    r9 = rlwinm(r9, 0, 31, 31);  // bit 0
    if (r9 == 0) goto loc_00566120;
```

**Il PPU aspetta che il bit 0 di `[0x20000000]` diventi 1: e' l'attesa del
completamento di un job SPU.** Il gioco e' fermo li' perche' quel flag non viene
mai scritto:

```
[WWATCH] write32 0x20000000 = 0x00000000  ra=...   <- solo il PPU, che lo azzera
=== DMA SPU verso 0x20000000..0x20000100 ===  0
```

Nessun DMA della SPU tocca quell'indirizzo. Il job viene caricato ed eseguito,
ma la fase di store / decremento della dipendenza non arriva mai a scrivere il
flag. E' li' che continua l'indagine.

## Sessione 17 — il segnale mancava sul serio, ma non dove lo cercavo

Due cose, e la seconda vale piu' della prima.

### La leva che avevo scambiato per una pista

`LBP_WS_DRAIN=2` fa ripartire un task SPU parcheggiato in WAIT_SIGNAL anche se
nessuno lo ha svegliato. Con quella leva attiva comparivano un DMA malformato
verso `ea=0x05000000` e un ciclo di syscall taskset, e la sessione 16 li aveva
indicati come "il fronte successivo". Un A/B a quattro configurazioni dice che
non lo erano:

| configurazione | righe | DMA malformati | syscall num=3 | ea=0x05000000 |
|---|---|---|---|---|
| base | 7917 | 0 | 0 | 0 |
| `SPURS_EF_FORCE=1` | 8011 | 0 | 0 | 0 |
| `LBP_WS_DRAIN=2` | 7935 | 8 | 23 | 0 |
| entrambe | 8052 | 8 | 23 | 9 |

Esistono **solo** con `LBP_WS_DRAIN`. Sono lo stato inconsistente prodotto dalla
mia stessa sonda: un task ripreso senza aver ricevuto i dati che aspettava
calcola parametri MFC da registri che non significano nulla. Inseguire quell'EA
sarebbe stato inseguire l'ombra della sonda.

### La causa vera: la coda lock-free non era una coda

`CellSyncLFQueue` era una struct dell'host scritta nella memoria del gioco:
`atomic_uint` nativi, little-endian, e un **puntatore host a 64 bit** dentro
l'oggetto guest. Ma il consumatore di quella coda e' codice SPU del titolo, che
legge gli stessi 128 byte con `GETLLAR` nel layout Sony, big-endian. I due lati
non potevano incontrarsi: il PPU spingeva in campi che per l'SPU erano rumore, e
l'SPU vedeva una coda per sempre vuota.

Da qui lo stallo che sembrava un segnale perso nei condvar. Il log lo diceva in
chiaro, bastava leggerlo nell'ordine giusto:

```
CreateTaskset  ea=0x2A86F380
EventFlagInitialize(dir=1 = SPU2PPU)  flagEA=0x2A870C80
LFQueueInitialize(queue=0x2A870D00 owner=0x2A86F380 size=32 depth=12 dir=2 = PPU2SPU)
CreateTask(id=0, arg=2A870D00 ...)          <- il primo argomento del task E' la coda
WAIT_SIGNAL#1 enter task=0 ran=0ms          <- pop su coda vuota, parcheggia subito
```

Il PPU aspettava l'event flag che quel task avrebbe alzato a lavoro finito; i
dieci thread in `sys_cond_wait` erano vittime a valle.

**Il segnale mancante c'era davvero, ma stava nella chiusura della push.**
`_cellSyncLFQueueCompletePushPointer` rilegge da `m_hs1` l'id del consumatore che
si era registrato prima di parcheggiare e chiama `fpSendSignal`; per una coda
SPURS quella callback e' `_cellSpursSendSignal` e `m_eaSignal` e' l'EA del
taskset. E' il risveglio, non la scrittura del dato, a far ripartire il task.

Il layout e' stato verificato due volte, da fonti indipendenti: RPCS3
(`Emu/Cell/Modules/cellSync.h`) e il codice SPU dell'immagine 1 del titolo, che
fa `GETLLAR` di 128 byte sulla coda e legge `m_h4` a +0x06, `m_h5` a +0x08,
`m_depth` a +0x14 e i tre campi da 5 bit di `push2` a +0x30 — esattamente
questi offset. Le due letture coincidono.

La read-modify-write del PPU prende lo stesso lucchetto che serializza gli
atomici SPU (`spu_atomic_line_lock`). Una `PUTLLC` a cavallo della nostra
scrittura fallisce da sola: la commit confronta la linea con lo snapshot preso
alla `GETLLAR`, che e' esattamente la semantica dell'hardware.

### Misura

A parita' di tetto di tempo, righe di avanzamento: **5.263 → ~1.980.000**.
Il titolo spinge 260 job, ogni push sveglia il task, carica `particle` (16 MB) e
`particle-ingame`, registra VOX e IGC, e arriva a `game/hud.cpp`.

### Le due leve non servono piu'

`SPURS_EF_FORCE` e `LBP_WS_DRAIN` erano sonde A/B nate proprio da questo stallo.
Con la coda vera non servono, e la misura lo dice senza margini: nella stessa
esecuzione, senza nessuna delle due, `cellSpursEventFlagWait` si blocca **zero**
volte e viene risvegliato 40. L'event flag ora lo alza il task, come deve.

Restano nel codice come sonde, ma nessun percorso normale le tocca.

### Il fronte successivo

Una chiamata indiretta irrisolta:

```
[ppu] unresolved indirect call -> 0x0048F74C (tid=1 lr=0x0048EFBC)
      CHAIN obj=250E6BC0  *(obj)=vtable=00000194
```

La riga `CHAIN` e' una sonda generica che prova a leggere r3 come oggetto C++:
qui `obj[0..7] = 00000194 00000000 00000000 00000003 0000000B ...` non e' un
oggetto con vtable, e' una struttura dati del sistema particellare. Il vtable a
zero e' un falso indizio.

Il vero problema e' piu' semplice: `0x0048F74C` non ha un ingresso liftato
registrato. Cade nella zona `ParticleDirectory` (fra
`InitializeGroupMaterialIds` a `0x48ED98` e `AddGroup` a `0x48FF48`), quindi e'
un ingresso alternativo dentro una funzione gia' liftata -- il caso per cui
esiste `find_indirect_entries.py`. Subito dopo il fallimento il titolo entra in
un ciclo infinito in `game/hud.cpp:845` ("delete an invalid pointer"): e' la
conseguenza, non la causa.

Nel resto dell'esecuzione: zero assert, zero NID irrisolti, zero `FIFO JUMP`,
zero accessi OOB. Manca solo `particles-cutscenes.bin`, che il titolo stesso
dichiara opzionale.

### Le tabelle di salto a offset: 2359 ingressi che non vedevamo

Liftato `0x0048F74C`, il giro dopo e' comparso `0x0048F718`: stesso chiamante,
0x34 piu' indietro. A quel punto invece di rilanciare il ciclo ho guardato il
disassemblato, ed era una tabella di switch:

```
0048F6C0:  extsw  r0, r0
0048F6C4:  add    r0, r0, r11        ; r11 = 0x48F6D0, base della tabella
0048F6C8:  mtctr  r0
0048F6CC:  bctr
0048F6D0:  .word 0x68 0x7C 0xB4 0xEC 0x68 0x124 0x68 ... 0x48
```

Le voci sono **offset relativi alla tabella**, non indirizzi:
`0x48F6D0 + 0x48 = 0x48F718`, `+ 0x7C = 0x48F74C`. `find_table_targets` cercava
parole che contenessero l'indirizzo del caso, e di quelle non ce n'e' nessuna:
l'intera famiglia era invisibile. Sei casi in questa tabella sola, **2359 in
tutto il binario**.

`find_offset_table_targets` li trova in un colpo. Il limite della scansione e' la
tabella stessa — non puo' arrivare oltre il suo primo caso, perche' quello e'
gia' codice — piu' tre vincoli: minimo due voci, offset locali (il corpo di uno
switch non e' mai lontano), destinazioni allineate e dentro un segmento
eseguibile. Sul caso noto trova esattamente i sei indirizzi giusti.

Lezione, e vale piu' del codice: inseguire i target irrisolti uno alla volta col
ciclo automatico costava un lift e una ricostruzione per caso. Guardare il
disassemblato una volta ne ha chiusi 2359.

Con quelli liftati (26.257 funzioni) il titolo va oltre le particelle: carica
`main/pak19/post`, **prefetcha i task del menu principale**
(`pre-game-introduction`, `main-menu-autosave-warning`, `main-menu-ndi`), apre
`sound1/speech/english.psarc`, e il kernel SPURS distribuisce workload veri
(`POKE wid=6 ready=8 ... state=2`). Il ciclo infinito di `hud.cpp` non c'e' piu'.

### Dove si ferma adesso

```
Assertion failed at job/src/ppu/jobapi/joblist.cpp(333): WwsJob_IsAligned(numElts, 16)
[cellGcmSys] fence queue overflow -- oldest dropped
[ps3] FAULT INSIDE THE GUEST WINDOW: SIGBUS at guest 0xD0000060
      guest caller: func_0073FDF0+0x4F0   lr=0x0073FF14
```

`func_0073FDF0` e' proprio la funzione che contiene quell'assert (il nome viene
da `names_from_asserts.py`). `0xD0000060` e' `VM_STACK_BASE + 0x60`, il fondo
della regione degli stack. Restano anche due chiamate indirette irrisolte:
`0x004C8834` e `0x006983FC`.

### Il trap del guest: correzione di una mia lettura

Il lifter ignorava `tw 31, rX, rX` con il commento "safe to no-op in recomp".
Avevo concluso che fosse il terminatore che il compilatore mette dopo una
chiamata noreturn, e che ignorarlo facesse eseguire codice morto -- attribuendo
il SIGBUS a questo. **Sbagliato**, e il disassemblato lo dice in tre righe:

```
0x4A5070  bl     ReportAssert        ; stampa e torna "continuare?"
0x4A5080  bne    cr7, 0x49EC80       ; si' -> prosegui
0x4A5084  tw     31, r1, r1          ; no -> fermati nel debugger
0x4A5088  b      0x49EC80            ; ...e poi prosegui lo stesso
```

Entrambi i rami finiscono a `0x49EC80`: e' il break del debugger dell'idioma di
assert di Naughty Dog, e senza debugger attaccato il titolo prosegue. Come
flusso, ignorarlo era corretto.

Come diagnostica no: un assert del gioco passava senza che nulla lo segnalasse
dal lato nostro, e i suoi effetti si vedevano molto piu' tardi come dati
sbagliati. Ora `ppu_guest_trap` riporta indirizzo, registri e stack guest e
prosegue; `PS3_TRAP_FATAL=1` ferma il processo quando serve lo stack del primo
assert e non quello che il titolo combina dopo averlo ignorato.

Il SIGBUS viene da altro: dai dati che l'assert WwsJob segnalava gia' come
sbagliati.

### Cosa ha fatto vedere subito

Il primo trap che scatta non e' quello di WwsJob:

```
*** ASSERTION:
***      File: void RenderTargetManager::CreateCorrespondenceTextureForRenderTarget(U32)
***      Line: 81
```

La condizione sta a `0x49EC78`, e la catena e' leggibile per intero:

```
0049EC44  bl     0x4C8168          ; RenderTargetManager::AllocateOne2DRenderTarget(...)
0049EC50  cmpwi  cr7, r3, -1       ; ha fallito?
0049EC74  lwz    r0, 0xE0(r4)
0049EC7C  bne    cr7, 0x4A5058     ; assert: [obj+0xE0] deve essere 0
```

E `0x004C8834` -- una delle due chiamate indirette rimaste irrisolte -- e' un
caso della tabella di salto **dentro `AllocateOne2DRenderTarget`**, a
`0x4C85F8`. La funzione saltava a un ingresso che non esisteva, tornava un
valore qualsiasi, e l'assert subito dopo lo segnalava. Con gli offset con segno
quel caso e' coperto.

### Dopo (26.650 funzioni liftate)

**Zero chiamate indirette irrisolte. Zero assert. Zero trap. Nessun crash.**
Sono spariti insieme, ed erano la stessa causa: due tabelle di salto che non
vedevamo. Il titolo prosegue, apre i banchi audio (`sound1/banks/common.bnk`) e
i thread `q1-worker` girano.

Si ferma su un'attesa nuova, e questa e' leggibile per intero:

```
0056611C  lwz    r11, 0x320(r3)     ; r11 = this->[0x320] = 0x20000000
00566120  lwz    r0,  0x0(r11)
00566124  lwz    r9,  0x0(r11)
00566128  rlwinm r9, r9, 0, 31, 31  ; bit 0
00566130  beq    cr7, 0x566120      ; gira finche' bit0 != 1
```

E' `func_00566108`, chiamata da `0x0053261C` -- codice di fisica, accanto a
`PhysIngameTest::FrameSyncBegin` (`0x531880`) e a `InitPhysSpuModules`
(`0x565468`). Il PPU aspetta che un job SPU della fisica alzi il bit 0 di una
parola condivisa a `0x20000000`, e quel bit non arriva mai. Poco sotto c'e' la
stessa attesa su `[r11+0xC]`, per una lista di job.

### I 28 moduli SPU dei job erano dentro l'EBOOT

La risposta alla domanda "il modulo SPU della fisica viene eseguito?" era: non
esiste nel port. `extract_spu_images.py` cerca ELF SPU annidati; i moduli del
job manager WwsJob non sono ELF. Sono immagini grezze con un header di 0x30 byte
che finisce col magic `0xC0DEC0DE`, e nell'EBOOT ce ne sono **28** -- circa
460 KB di codice SPU del titolo, fisica e render e animazione, che il port non
aveva mai visto.

Il layout e' stato risolto sul binario, non preso da una specifica. Nel primo
modulo la tripla PIC

```
ila  $r2, 0x41C0
brsl $r126, <istruzione seguente>
sf   $r126, $r2, $r126
```

fissa l'indirizzo di link dell'istruzione dopo il `brsl`. Delle tre ipotesi su
dove cominci l'immagine, una sola da' un numero tondo -- 0x4000, che e'
esattamente il campo a +0x28 dell'header -- ed e' che l'immagine parta da
`module_start`, header incluso. Con quella l'entry cade su un prologo pulito
(`stqd $r80..$r126`, `rdch $r82, SPU_RdDec`). Tutti e 28 escono coerenti.

Il layout coincide con quello che `lift_jobmods.py` gia' assumeva per LBP: e'
una conferma indipendente, non una coincidenza. Quel tool sapeva gia' fare
questo lavoro ma aveva le cartelle di LBP cablate; ora e' parametrico.

**28 moduli, ~11.000 funzioni SPU liftate**, registrate per firma di 16 byte
sotto gli image id 200..227.

### E l'ingresso che mancava al policy module

Con i moduli registrati il job manager cadeva in `BRANCH-TO-0` a LS `0xA2C`,
dodici volte per esecuzione. `0xA2C` sta dentro `func_00000A00`: e' la
`brasl $r79, 0x3FEC0` che chiama lo stub di salvataggio registri che il modulo
si genera da solo in cima alla local store. `--auto-functions` non puo' vederlo
-- ci si arriva solo con un salto calcolato. `--extra-funcs 0xA2C` e sparisce.

I passi del lift SPU vivono ora in `uncharted-data/relift_spu.sh`: i file che
producono non stanno in nessun repo, e senza lo script si perdono i parametri.

### Dove siamo fermi

Il job manager gira pulito, ma **nessun modulo di job diventa mai residente**:
non compare una sola riga `[spu-ovl]`. Il kernel dispaccia i workload a ogni
passo, il PPU scrive `ready=8` su wid=6, e il modulo esce senza aver caricato
niente. Il PPU resta sull'attesa della fisica a `0x20000000`.

L'argomento per-workload arriva, ed e' valido. Il dump della job list a ogni
esecuzione del modulo (`SPURS_PM_TRACE=1`) mostra sei liste distinte, e una ha
comandi veri dentro:

```
RUN#8192 wid=7 data=0x108DFD00
  joblist@0x108DFD00:  1008EB00 00002F80 00000000 0000017C
                       00060001 0000FFFF FFFFFFFF FFFFFFFF
                       00900001 108E0900  00900001 108F0580
                       00900001 108E2390  00900001 108F0620
```

`00900001` piu' un EA, quattro volte: sono comandi WwsJob. Le altre cinque liste
hanno solo l'intestazione (`1008EB00` = l'EA del policy module, `0x2F80` = la sua
dimensione) e nessun comando.

Due cose da notare, entrambe misurate:

1. **Nessun DMA va mai a LS 0x4000.** Il manager legge e scrive solo le strutture
   di controllo (LS 0xB00, 0xD00, 0x14C0 da/verso `0x2AE53E80`). Non arriva mai a
   mettere in scena del codice, quindi la firma dei moduli non ha nulla da
   riconoscere -- non e' il riconoscimento a mancare. **Questa resta la domanda
   aperta**, ed e' sul protocollo dei comandi WwsJob, non sulla schedulazione.

2. **Il workload con i comandi non viene quasi mai eseguito.** Attenzione a come
   si arriva a questo numero: la riga `RUN#` e' campionata (le prime 64, poi una
   ogni 4096), e contare quelle righe e credere di aver contato le esecuzioni e'
   lo stesso errore che ha prodotto la diagnosi sbagliata della sessione 15. Con
   un contatore vero, per 100 secondi:

   ```
   [spurs-pm] esecuzioni per wid: 0=868 1=882 2=177 3=169 4=210 5=1010 6=779
   ```

   In quella esecuzione wid=7 non compare. Ma anche questa e' una lettura da un
   campione solo, e in una esecuzione da 90 secondi il quadro e' diverso:

   ```
   esecuzioni per wid: 0=1681 1=1685 2=339 3=331 4=422 5=1977 6=1603 7=156
   ```

   **wid=7 gira eccome**, 156 volte. Il numero cambia da un run all'altro perche'
   il gioco abilita i workload mentre carica, e una finestra corta puo' finire
   prima. Con una traccia esplicita su quale delle tre condizioni ferma un
   workload (`SPURS_WKL_GATE=1`), nessun workload abilitato viene mai scartato.

   Quindi: **tutti e otto i workload girano, wid=7 compreso, e nessun modulo di
   job viene comunque messo in scena.** Il problema non e' la schedulazione.

Gli EA dei comandi (`0x108E0900`...) NON stanno nell'immagine dell'EBOOT: i 28
moduli mappano a VA `0x100B2C00`..`0x10171CC0`, e questi sono oltre. Sono
puntatori a memoria allocata a runtime -- il titolo copia altrove cio' che passa
al job, oppure `0x00900001` non e' il comando "carica codice" ma un altro.

## Sessione 16 — CORREZIONE: non manca nessun segnale

**La diagnosi della sessione 15 era sbagliata, ed e' stata smontata dai log
stessi.** Va letta come esempio di come si sbaglia una misura.

Avevo concluso "manca un segnale" perche' la run con `PS3_COND_SPURIOUS_MS=250`
faceva 10456 righe contro 7912. Ma quelle righe in piu' **sono la sonda che
stampa se stessa**: ogni waiter si sveglia ogni 250 ms, ri-testa il predicato, lo
trova falso e rientra in `sys_cond_wait`, che stampa una riga `[WAIT]`. Contando
solo le righe che NON sono rientri in attesa:

| run | totali | cond_wait | resto |
|---|---|---|---|
| normale | 7912 | 2649 | **5263** |
| con risvegli spuri | 10456 | 5193 | **5263** |
| con broadcast | 7921 | 2658 | **5263** |

Identiche. E l'ultima riga di progresso e' la stessa in tutte e tre. La sonda non
ha fatto avanzare il titolo di un passo.

Il verdetto giusto e' l'altro ramo di quello che il commento in `sys_cond.c`
descrive: **il predicato non e' mai vero, e va inseguito il produttore.** Il che
combacia con la sonda `PS3_COND_PRED` scritta nella stessa sessione, che per
cond 44 riporta il byte del predicato a **0 a ogni singola attesa**.

**Anche il conteggio wait/signal era un artefatto.** `cond_signal` stampa solo le
prime 80 chiamate e `cond_signal_all` le prime 40 (cap nel codice): le 120 righe
`[SIGNAL]` finiscono a riga 1125 su 7912, e da li' in poi i segnali continuano
senza essere stampati. Il rapporto "1321 attese contro 18 segnali" non dice nulla.

### Dove e' davvero lo stallo

    [cellSpurs] EventFlagWait BLOCKED 24s on pattern 0x0001 flagEA=0x2A870C80
                -- waiting for an SPU workload to cellSpursEventFlagSet it
    [spu_workload] task 0 (taskset 0x2A86F380) sleeping 16s in WAIT_SIGNAL

Attesa circolare nel livello SPURS/SPU: il thread PPU aspetta che un workload SPU
metta il bit 0x0001, e il task SPU che dovrebbe metterlo e' parcheggiato in
WAIT_SIGNAL aspettando un segnale che nessuno manda. **I dieci thread in
`sys_cond_wait` sono vittime a valle**, non la causa.

### Le due leve, misurate

Entrambe sono sonde A/B, non fix: **non vanno lasciate in una run che si misura**
e non sono in `run.sh`. Contando le righe di progresso (totali meno i rientri in
attesa, baseline **5263**):

| leva | resto | effetto |
|---|---|---|
| `LBP_WS_DRAIN=2` | 5283 | il task SPU si sblocca, il PPU resta fermo |
| `SPURS_EF_FORCE=1` | 5343 | il PPU si sblocca, poi si riblocca (waits=1000) |
| entrambe | **5367** | l'immagine SPU del titolo esegue e un job arriva a fine |

Che il titolo avanzi solo forzando l'event flag conferma che **dipende davvero da
quel workload**: non e' un'attesa spuria.

### Il fronte successivo

Con entrambe le leve compare il vero ostacolo a valle:

    [spu] SPURS taskset syscall num=3 (raw=0x3 args=0x00000A70) image=1
    [spu-dma] img=1 pc=0x09900: 256 identical transfers to ea=0x05000000 -- halting the SPU
    [spu_workload] async image=1 RETURNED rc=0 (job ran to completion, did not loop)

**L'immagine SPU del titolo (image=1, quella estratta dall'EBOOT) esegue.** Ma un
suo job ripete 256 DMA identici verso `ea=0x05000000` e il runtime lo ferma.

`0x05000000` sta nella memoria principale PS3 ma fuori da tutto cio' che il
titolo alloca (IO a `0x21000000`, heap fino a ~`0x2B200000`): e' l'indirizzo che
il job riceve, non uno che ha calcolato da un puntatore valido. Il DMA parte
dalla funzione SPU a `0x9DA0`, chiamata da `0x098FC`; al ritorno il codice
compone un comando MFC (`0x80410900`).

Da qui: trovare chi riempie il descrittore del job con quell'EA -- il kernel
SPURS, il job manager del titolo, o una nostra struttura HLE che lo passa male.

## Sessione 15 — la diagnosi sbagliata, tenuta come riferimento

Il titolo carica i pacchetti di livello via I/O asincrono:

    ### SyncLoad (loado) on [particle]
    >>> PackageMgr::RequestLevel [particle]
    !    OpenAsync: file:".../pak19/particle.pak" issued:1
    <   PreadAsync: size:0x00100000 offset:0x00000000 ... issued:1

`issued:1` -- prima era 0. Poi si ferma: **7912 righe a 90s, 7912 a 240s, 7912
a 390s**. Non e' lentezza, e' uno stallo.

**Non e' un deadlock circolare come sembrava.** Il log mostra il PPU in attesa
di un event flag SPU2PPU e un task SPU in WAIT_SIGNAL, ma i due interruttori del
progetto per quella classe (`SPURS_EF_WAKE_PARKED`, `SPURS_EF_OBJ_DELIVER`) non
cambiano nulla: con e senza, il titolo arriva identico a `particle.pak`, stesse
letture asincrone, stessi 518 riferimenti. Quel thread e' un worker in attesa,
non il titolo fermo. Il policy module SPU intanto fa 90 esecuzioni e 24 uscite
pulite al kernel in 100 secondi.

**La misura che conta.** `PS3_COND_SPURIOUS_MS` e `FLOW_CONDKICK` erano
Win32-only; portate su POSIX, danno la diagnosi:

| run | righe |
|---|---|
| normale | 7912 |
| con risvegli spuri (250 ms) | **10456** |

Il titolo avanza solo con la sonda: **manca un segnale, e il predicato era gia'
soddisfacibile**. Se non fosse mai vero, la sonda non cambierebbe nulla.

Dieci thread guest sono fermi in `sys_cond_wait`. Il conteggio per condvar:

| cond | wait | signal |
|---|---|---|
| 44 | 1321 | 18 |
| 42 | 879 | 74 |
| 1 | 438 | 20 |

Nessuna e' orfana, quindi il produttore esiste e segnala -- ma un waiter non
riceve mai il suo.

**Ipotesi scartata:** che `pthread_cond_signal` svegliasse un waiter arbitrario
dove lv2 ne sveglia uno specifico. Provato il broadcast: 7921 righe, nessuna
differenza. Rimosso.

**Da qui:** trovare quale segnale si perde. Il candidato e' una race fra il
rilascio del mutex e il signal, oppure un produttore che segnala prima che il
consumatore parcheggi -- e a quel punto su PS3 il segnale sarebbe perso lo
stesso, il che punterebbe invece a un predicato che il consumatore legge da
memoria che il produttore non ha ancora reso visibile.

## Sessione 14 — il titolo legge i propri dati

Le correzioni della notte, in ordine di quanto hanno sbloccato:

**1. Le code lock-free SPURS non esistevano.** `_cellSpursLFQueueInitialize` e
`cellSpursLFQueueAttachLv2EventQueue` tornavano *unresolved NID* a ogni avvio.
Sono il canale con cui il lato PPU consegna lavoro al proprio lato SPU: senza
di loro il kernel SPURS vedeva `ready=0` per sempre. Il lato SPU girava da
sessioni; non gli arrivava niente. Implementate come wrapper sulle `cellSync`
LFQueue, che il runtime aveva gia' complete.

**2. I callback durante uno spin.** `SwapBuffers` aspettava il proprio handler
vblank, ma cellGcmSys li consegna solo ai confini delle chiamate HLE e
quell'attesa e' un loop di lettura puro. Ora `vm_read32` chiama il pump dopo
4096 letture uguali.

**3. Il file system del titolo.** `/dev_hdd1` — il disco scratch, dove il FIOS
di Sony tiene la cache — non aveva mapping e cadeva sull'ISO in sola lettura.
Aggiunto `$PS3_HDD1_ROOT`; e un `open` con `O_CREAT` ora crea le directory
genitore, che su una PS3 esistono gia' perche' le possiede il sistema.

**4. Lo stack dei thread guest, 8x.** Il codice liftato costa circa quattro
volte lo stack del PowerPC originale: ogni funzione tiene il frame per l'intera
chiamata e gli helper del runtime ne aggiungono di propri. Il thread "Load Text"
chiede 16 KB e ne consumava 61. `$PS3_STACK_SCALE` lo regola.

**Dove e' arrivato:**

    [fs] open '/dev_bdvd/PS3_GAME/USRDIR/build/main/text1/2.subtitles' -> fd 89
    <  ReadSync: size:0x00017c2f read:0x00017c2f ... result:kNOERR
    ?   StatSync: file:".../dc1/anim-descendant.bin" result:kNOERR
    [RSX] CLEAR_SURFACE ...   (un frame dopo l'altro)

Apre, legge e chiude i propri file — testi, sottotitoli, animazioni — mentre il
ciclo di frame gira e gli SPU eseguono task. Log fino a 5206 righe (era 500).

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

## Sessione 13 — il primo frame, e il file system del titolo

**Il flip.** Il titolo arrivava a `SwapBuffers` e restava a girare su una propria
variabile globale che solo il suo handler vblank puo' far avanzare. Ma
cellGcmSys consegna quegli handler da `ppu_gcm_pump`, che `ps3_hle_call` invoca
**ai confini delle chiamate HLE** — e l'attesa di SwapBuffers e' un puro loop di
lettura, senza chiamate HLE. Nessun pump, nessun handler, attesa infinita.

`vm_read32` ora chiama il pump quando la stessa lettura si e' ripetuta 4096
volte: abbastanza da essere uno spin e non un campo caldo. Lo spin sparisce
(HOTREAD da centinaia a **zero**) e il titolo esce dall'attesa del flip.

Prima di quel punto il titolo aveva gia' fatto la sua prima `CLEAR_SURFACE` e
chiesto due flip: il percorso grafico funziona.

**Il file system.** Subito dopo il titolo avvia i thread del FIOS di Sony (si
vedono i suoi `cond_wait`/`cond_signal` fra quattro thread) e cerca il proprio
indice di cache su `/dev_hdd1`. Quel mount point non aveva mapping: cadeva sulla
VFS root, cioe' sull'immagine del disco, in **sola lettura**. Ora c'e'
`$PS3_HDD1_ROOT`. E un `open` con `O_CREAT` su una directory inesistente crea i
genitori e riprova — su una PS3 quell'albero esiste gia' perche' e' del sistema.

**Dove si ferma.** Ancora una chiamata indiretta non risolta, ora a `0x0066B458`,
nella regione FIOS del titolo: il dispatch non trova nulla, r3 resta `0xFFFFFFFF`
e il gioco lo usa come puntatore (SIGSEGV a guest `0xFFFFFFFF`, intercettato
dalla trap con contesto completo).

Invece di inseguirli uno alla volta, l'ultimo lift copre **tutti i 341 blocchi
raggiungibili solo per calcolo nella regione 0x650000-0x680000**, dove i target
si concentrano.

## Sessione 12 — il gioco carica i livelli

**Il loop dell'inizializzazione grafica era un bug mio.** In sessione 7 avevo
aggiunto i 501 ingressi indiretti con `{"start": va, "end": va}`: un intervallo
vuoto. Per una funzione senza corpo il lifter emette

    void func_004C5464(ppu_context* ctx) {
        { g_trampoline_fn = (void(*)(void*))func_004C5464; return; }
    }

cioè una funzione che chiede al dispatcher di richiamare se stessa. Uncharted ci
girava dentro per sempre, in `RenderTargetManager::CreateCorrespondenceTexture-
ForRenderTarget`, con gli stessi argomenti a ogni giro (col debugger:
`r3=0x50000 r4=0x500 r7=0x2AE47920` invariati).

Lezione di metodo: la sparizione di *"unresolved indirect call"* mi aveva
convinto di aver risolto, mentre avevo solo creato funzioni vuote. **Il sintomo
era migliorato e la causa peggiorata.** Da qui in avanti si verifica il C
generato, non solo la scomparsa del messaggio.

Corretto l'intervallo (ogni voce arriva alla fine della funzione che la
possiede), il titolo esce dall'inizializzazione e arriva a:

    ### SyncLoad (loado) on [main/misc1/debugrendering6]
    >>> PackageMgr::RequestLevel [main/misc1/debugrendering6]
    OpenAsync: "/PS3_GAME/USRDIR/build/main/misc1/debugrendering6.pak"
    [spu_workload] task 0 (taskset 0x2A8CE880) sleeping 1s in WAIT_SIGNAL
    ### SyncLoad (loado) on [main/pak19/post]

**Due livelli richiesti in sequenza, e task SPU in esecuzione con attesa di
segnali.** Log da 500 a 1268 righe.

### Altre due classi di ingresso

`find_indirect_entries.py` ora riconosce anche i **target di jump table** (487:
indirizzo contenuto in una word dei dati *e* preceduto da un terminatore) e, con
`--computed-blocks`, i blocchi che nessun branch nomina (9713 — corretto ma il
lift esplode, quindi spento di default).

Per il caso pratico c'è `--extra`: si passa l'indirizzo che una run ha appena
riportato. È il ciclo di lavoro: il gioco dice cosa gli manca, glielo si dà.

### Dove si ferma

Restano 8 target irrisolti (aggiunti con `--extra` nell'ultimo giro) e 26
`bctr to NULL`, 20 dei quali da `func_00764998+0x3B0` — il gioco salta a un OPD
nullo, cioè un puntatore a funzione che nessuno ha inizializzato. Poi un SIGSEGV
con `sp=0xFFFFFF30`, uno stack pointer corrotto, in `func_000B1008`.

Il prossimo filo è il `bctr to NULL`: capire chi dovrebbe riempire quel
puntatore.

## Sessione 11 — chi scrive LS 0x12A0 / 0x12C0, e dove sta davvero il PPU

**Risposta alla domanda: le scrive il policy module stesso.** Non sono una
casella postale in attesa di un DMA esterno: `stqa` verso 0x12A0 a LS 0x24CC,
0x2F2C, 0x327C, 0x33DC; verso 0x12C0 a 0x1A7C, 0x2D04, 0x32DC. Sono le variabili
di stato della sua macchina a stati, e il loop a 0x2DE0 è la sua attesa
legittima di lavoro da fare.

Quindi il modulo SPU non è bloccato: è **inattivo perché non arrivano job**. Il
lavoro si è spostato di nuovo sul PPU.

**Il PPU non è in deadlock.** Il rilevatore del progetto (`YDKJ_STUCKMAX`,
default 2000 chiamate identiche di fila) non scatta mai: i target cambiano, il
titolo sta lavorando.

**E ora sappiamo su cosa.** Un EBOOT retail è stripped, ma Uncharted è compilato
con gli assert, che portano `__PRETTY_FUNCTION__`. Il nuovo
`tools/names_from_asserts.py` li risolve — devono essere seguiti in due passi,
perché il puntatore alla stringa sta a un offset rispetto a una base che a sua
volta viene dal TOC — e recupera **1772 nomi su 14605 funzioni**. Lo stack che
sembrava anonimo si legge così:

    Err BigInit()
      bool PlatformInit(U32, U32, U32, bool, bool)
        void RenderTargetManager::CreateCorrespondenceTextureForRenderTarget(U32)

Vicine: `RenderTargetManager::AllocateOne2DMsaaRenderTarget` e
`RenderSurfaceManager::AllocateOneRenderSurface`.

Il titolo è ancora dentro **l'inizializzazione grafica**, non nel game loop — e
il vecchio errore "dont have enough space for primary render target" non compare
più: le allocazioni riescono.

**Ipotesi in verifica:** è lentezza, non stallo. I chunk liftati erano compilati
a `-O0`; 2,16 milioni di righe di C++ non ottimizzato rendono l'inizializzazione
di un motore lentissima. Ricompilazione a `-O1` in corso (`build.sh` aggiornato).

## Sessione 10 — il policy module gira davvero

Due errori, uno mio e uno di configurazione, tenevano fermo il kernel SPU.

**1. La base di caricamento sbagliata (mio errore).** Avevo incapsulato il
policy module con `--base 0`, ma il runtime lo carica a **LS 0xA00** (il trace
lo diceva: `entry=0xA00`). Il codice liftato aveva quindi indirizzi 0-based
mentre i salti indiretti a runtime usano indirizzi LS reali: ogni target cadeva
0xA00 più in là del dovuto. Il sintomo era un salto a `0x2DE0` ripetuto
**4 milioni di volte**. Rifatto con `--base 0xA00 --entry 0xA00`, l'entry
liftata diventa `spu_func_00000A00` e i salti si risolvono.

Nota diagnostica: `0x2DE0` non era corruzione. Con la base giusta si legge
`ila $r0, 0x2DE0` — il modulo si imposta da sé quell'indirizzo di ritorno. Era
il *nostro* lift a non avere una funzione lì.

**2. `SPU_TAILRET=1`.** Senza, i ritorni del codice liftato annidano frame host
e il policy module supera i 2000 di `SPU_HOST_DEPTH_MAX`: *"lifted call
recursion, halting the SPU"*. Con la variabile attiva: **zero halt**.

Risultato — il ciclo kernel↔policy module funziona:

    [spurs-pm] RUN#1  wid=0 ... image=2 entry=0xA00
    [spurs-pm] exit-to-kernel#61 (r3=0x00000000 polls=0)
    [spurs-pm] END wid=1 status=0x2 pc=0x009C0 polls=0 exited=1

**24.576 esecuzioni su tre workload** (wid 0, 1, 2), con entrate e uscite
pulite verso il kernel. Il log passa da 419 a 758 righe.

I parametri necessari vivono ora in `uncharted-data/run.sh`, non nella memoria
di nessuno.

### Dove si ferma adesso

I run finiscono con `pc=0x02DE0 exited=0`: il modulo resta nel proprio loop di
polling, che legge LS `0x12A0` e `0x12C0` e aspetta che cambino. `polls=0`
significa che non chiama mai `selectWorkload`, quindi non è il kernel che deve
svegliarlo: aspetta un dato che deve arrivargli per DMA — presumibilmente dal
Job Manager lato PPU.

## Sessione 9 — SBLOCCATO: il kernel SPURS gira

**Uncharted abilitava i workload correttamente da sempre.** Lo stato nella
struttura SPURS era già 2 (*enabled*). Il problema era che **su POSIX nessuno
guardava**: le 225 righe che contengono `spurs_kernel_thread` e
`spurs_pm_worker` stavano dentro un `#ifdef _WIN32`, e così la `CreateThread`
che avvia il poller. `cellSpursAddWorkload` registrava il workload e nulla lo
dispacciava mai — mentre il log stampava comunque "kernel poll".

Il blocco non era Win32 per sostanza: usa `DWORD`/`WINAPI`/`LPVOID`/`HANDLE`,
`Sleep`, `CreateThread`, `CloseHandle` — tutti già in `win32_compat.h` — più
`SetThreadStackGuarantee`, che mancava e ora è un no-op.

Una conseguenza andava sistemata: il percorso del policy module **aspetta** i
suoi worker per-lane con `WaitForMultipleObjects`. I thread del compat erano
detached, quindi il dispatcher sarebbe tornato prima che le lane finissero. Ora
sono joinable e `HANDLE` porta il `pthread_t`.

Risultato:

    [spurs-kern] "" poll thread live: ea=0x1057BF80 pm=enabled
    [spurs-kern] "" dispatch wid=0 (enabled, state=2) image=2 ready=0
    [spurs-pm] DMA cmd=0x40 lsa=0x00B00 ea=0x02AE53E80 size=0x100 tag=8

Il codice SPU ricompilato **esegue** e fa DMA fra local store e memoria guest.

Una conferma che vale la pena registrare:

    [cellSpurs] wid=0 PM resolved (fp=0xABE4C07881298FE5 image=2)

`0xABE4C07881298FE5` è il fingerprint **del payload**, il secondo che la
sessione 8 ha insegnato al generatore a emettere. Con il solo fingerprint del
file ELF il policy module non sarebbe mai stato risolto.

### Il prossimo anello

Il kernel SPU entra, fa il suo giro (~20 DMA) e trova `ready=0`: **nessun job in
coda**. Il Job Manager lato PPU non ne ha ancora accodati, perché è nel suo loop
di costruzione comandi. Il log resta a 414 righe.

Da capire: perché il Job Manager PPU non pubblica job. Le due estremità ora
esistono e comunicano (il kernel legge la coda dove il PPU dovrebbe scrivere),
quindi la domanda è diventata specifica — che cosa manca fra il loop del PPU e
l'accodamento.

## Sessione 8 — cosa aspettava il gioco

**Non aspetta: gira a vuoto.** 101% di CPU, log fermo a 389 righe per cinque
minuti. Il thread guest alterna fra `ps3_indirect_call` e `func_004C5464` —
codice che scrive campi in una struttura, cioè costruisce comandi in un loop,
in attesa di risultati che gli SPU non producono mai.

**Perché gli SPU non producono nulla.** Il programma SPU non era liftato, e non
bastava liftare quello dell'EBOOT: `extract_spu_images.py` ne trova uno solo
(39.608 B a `0x845000`), ma quello che il gioco passa a `cellSpursAddWorkload`
è un altro — **12.160 byte assemblati in memoria a `0x1008eb00`**, byte SPU
grezzi senza intestazione ELF.

Ora entrambe sono liftate e registrate: **558 funzioni** l'immagine dell'EBOOT
(88,2% del testo), **234** il policy module SPURS.

Tre cose sono state aggiunte per arrivarci:

- `SPU_PM_DUMP=<dir>` — dumpa il policy module così come il titolo lo registra;
  è l'unico modo di ottenere quei byte, che nell'eseguibile non esistono.
- `build_spu_workloads.py` registrava **solo** il fingerprint del file ELF. Un
  titolo che consegna il payload grezzo non sarebbe mai stato trovato dal
  dispatch, che fingerprinta i byte che riceve. Ora emette anche il fingerprint
  del segmento PT_LOAD.
- `boot_main` chiama `spu_register_all()` se il port l'ha generata (weak).

**Dove si è fermato.** I workload sono registrati (`[boot] lifted SPU workloads
registered`), ma il poller del kernel SPURS non li dispaccia: nel log non
compare mai `[spurs-kern] ... dispatch wid=N (enabled, state=2)`. Il gioco non
ha ancora portato i workload allo stato *enabled* — `cellSpursAddWorkload` li
registra soltanto, mentre il dispatch vero parte da `cellSpursCreateTask`
(percorso che il Job Manager di Naughty Dog non usa) o dal poller quando lo
stato lo consente.

**Il prossimo passo:** capire come Uncharted abilita i suoi workload — quale
scrittura nella struttura SPURS o quale chiamata (`cellSpursWakeUp`?) manca dal
nostro percorso. Da lì il policy module gira, e con lui il Job Manager.

Utile: `SPURS_KERN_WATCH=1` traccia il poller, `SPURS_FORCE_SPUS=<n>` forza il
numero di lane.

## Da qui

Il gioco completa la configurazione di memoria e video, poi resta in attesa
senza produrre errori (il processo va ucciso, non termina da solo). Il prossimo
passo è capire cosa aspetta: quasi certamente il completamento di un job SPU —
la Fase 5, il muro previsto fin dall'inizio.

Il lift ora si fa così:

```bash
python tools/find_indirect_entries.py $D/EBOOT.elf \
    --functions $D/analysis/EBOOT.functions.orig.json \
    -o $D/analysis/EBOOT.functions.json
python tools/ppu_lifter.py $D/EBOOT.elf \
    --functions $D/analysis/EBOOT.functions.json \
    --hle-stubs $D/analysis/EBOOT.imports.json --toc 0x7d3190 -o $D/recomp
```

## Storico del blocco 3 Il gioco ora arriva più avanti e si lamenta da solo:

- `dont have enough space for primary render target` — non riesce ad allocare
  la VRAM, benché `cellGcmGetConfiguration` dichiari 256 MB a `0xC0000000`;
- `Mapped mesh output buffer of 8MB at 52656e64 to offset FFFFFFFF` — nota che
  `52656e64` è ASCII **"Rend"**: il gioco passa come indirizzo un campo che
  contiene testo, quindi legge una struttura dal posto sbagliato (o una nostra
  API gliela riempie male), e `cellGcmAddressToOffset` risponde -1;
- `Assertion failed: WwsJob_IsAligned(numElts, 16)` in `job/src/ppu/jobapi/
  joblist.cpp:333` — l'assert del job system WwsJob.

I tre sintomi sono probabilmente la stessa causa: una struttura che riempiamo
con offset o layout sbagliati. Il prossimo passo è trovare quale API restituisce
la struttura che contiene "Rend".

## I tre blocchi originali (storico)

1. **`NSWindow should only be instantiated on the main thread`** — con finestra
   il processo muore: il backend crea la finestra da un thread guest. Serve
   marshalling su main thread (`dispatch_sync` sulla main queue). Headless
   aggira il problema ma non è la soluzione.
2. **`FIFO JUMP to unmapped IO`** — il gioco salta a un indirizzo IO che il
   nostro `cellGcmSys` non ha mappato: da qui non escono più comandi.
3. **Gli `[vm] OOB access 0xFFFFFExx`** — decine di accessi a indirizzi vicini a
   -1: puntatori non tradotti o parametri opzionali passati male.

## Il blocco storico (risolto, tenuto come riferimento)

Crash in `vm_write64` da un thread del Job Manager (`EXC_BAD_ACCESS`,
`KERN_PROTECTION_FAILURE`). L'indirizzo host cambia a ogni run per via
dell'ASLR ma la parte bassa è costante — **guest EA `0x014000`**, dentro il
primo segmento di codice.

Da chiarire: quell'indirizzo è dentro `ppu_vm_size` (altrimenti `vm_oob()` lo
avrebbe respinto) e il loader non chiama `mprotect`, quindi la pagina dovrebbe
essere scrivibile. Nota la differenza fra le due piattaforme in
`boot_main.cpp`: Windows **riserva** 4 GB e committa on demand da un vectored
exception handler, POSIX fa un `calloc` di 0xE0000000 e basta.

**Il prossimo lavoro è diagnostico, ed è già identificato dalla sessione 1:**
implementare su POSIX `ps3_install_guest_ptr_trap` (oggi uno stub vuoto in
`ppu_loader.cpp`) come `mmap(PROT_NONE)` più un handler SIGSEGV/SIGBUS che
stampi EA, `vm_base` e il chiamante guest. Senza quello si procede a tentoni.

## Cosa manca ancora al backend Metal

vertex program del guest in MSL; depth/stencil, MRT, render-to-texture; il
crossbar `TEXTURE_CONTROL1` e i sampler per unità.

## Come si ricostruisce il port

```bash
~/Documents/Coding/uncharted-data/build.sh
cd ~/Documents/Coding/uncharted-data
PS3_HLE_INJECT_BASE=0x50000000 PS3_VFS_ROOT=/tmp/uc_iso \
    ./uncharted ~/Documents/Coding/uncharted-data/EBOOT.elf
```

La tabella NID è **per-port**: `ppu_hle_register_all()` nel runtime è weak e
vuota, e senza `tools/gen_hle_nids.py --all` ogni import resta non risolto.
