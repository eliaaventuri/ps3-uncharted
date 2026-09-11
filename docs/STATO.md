# Stato del progetto

Ultimo aggiornamento: **12 settembre 2026** — sessione 27.

## Dove siamo

**Uncharted: Drake's Fortune esegue su macOS.** Non è giocabile, ma dalla
sessione 17 il canale di lavoro PPU→SPU funziona davvero: il titolo carica i
pacchetti dei livelli, spinge job nella propria coda lock-free, ogni push sveglia
il task SPU che li consuma, e arriva a eseguire il proprio codice di HUD. Il
percorso completo — ELF decifrato → lift → compilazione nativa ARM64 →
esecuzione — funziona.

**Dalla sessione 19 il titolo disegna in continuo.** Il blocco storico
`Rendering is stuck on reference value N` è risolto: i fotogrammi di comandi RSX
drenati per corsa passano da ~75 a **~6.000 in 200 secondi (≈30 al secondo)**, e
in una corsa da 600 secondi non si è mai fermato. Superata quella soglia, il
gioco carica **contenuto vero** che non aveva mai raggiunto: pacchetti di livello
(`PackageMgr::RequestLevel [particle-ingame]`), script particellari, 642
cinematiche in-game, 829 voci, sottotitoli, e le icone dell'HUD (`assault-rifle-a`,
`bullets`, `treasure`).

**Dalla sessione 20 il gioco non si ferma più dopo il caricamento**: lo stallo
sulla parola a `0x20000000` era il job manager WwsJob che, con la local store
conservata da una corsa all'altra, non rileggeva la job list dopo il
`ResetList` del PPU. Con `SPURS_PM_FRESH=1` (ora default in `run.sh`) fa 500+
fotogrammi senza fermarsi. Il secondo blocco, sul lato RSX ("Rendering
is stuck on reference value N"), era un ciclo di JUMP che il rilevatore non
prendeva per collisione di hash: ora recuperato a budget esaurito, zero stalli
in 8 corse. Dettagli nella sezione "Sessione 20".

**Dalla sessione 21 il renderer riceve davvero i disegni.** Lo schermo nero non
era il backend Metal: il titolo disegna in **modalità immediata** (ogni vertice
scritto come registri fra BEGIN e END, mai un `DRAW_ARRAYS`) e quel percorso non
era implementato — al backend arrivavano **zero** draw.

**Dalla sessione 22 il port disegna la prima immagine vera.** Fra i vertici e lo
schermo c'erano altri tre difetti in fila, ognuno sufficiente da solo a fare uno
schermo nero: i passaggi fuori schermo finivano sul drawable e cancellavano
l'immagine (manca(va) il render-to-texture); le texture dichiarate come **array**
nel fragment program del guest tornavano zero perché legavamo solo gli slot
residenti; e le texture **DXT1** venivano copiate compresse dentro una RGBA8,
cioè rumore. Risolti tutti e tre, il titolo disegna un oggetto dorato con
dettaglio e luce — esce schiacciato in una striscia, ed è il prossimo dettaglio.
Dettagli in "Sessione 22".

**Dalla sessione 23 lo stallo intermittente dopo il caricamento non c'e' piu'.**
Non era il gioco: erano due tabelle globali da 8 slot nel runtime SPU (motori
MFC e salvataggi registri degli interrupt) condivise fra tutti i thread host,
mentre i 9 job manager WwsJob di Uncharted girano su 5-6 lane persistenti
l'uno. Chi restava fuori dagli 8 slot finiva su un motore condiviso, e un
aggiornamento perso lasciava un tag DMA incompleto per sempre: la lane girava,
il kernel thread non tornava, il PPU aspettava all'infinito. Ora sono per
contesto: **5 corse consecutive da 240 s senza stallo contro 4 su 4 prima.**
Corretti anche il layout di `CellPadInfo` (il titolo credeva di non avere il
controller) e tre import del pad e uno della tastiera che mancavano. La
"striscia" della sessione 22 non era un difetto: e' il fotogramma 6 della
moneta che gira (sprite sheet `boot1/coin.dds`, 32 pose da 128x128). Il fronte
ora e' il walker della FIFO RSX, che ricomincia i segmenti da capo migliaia di
volte e il titolo stampa `Command context has run out of memory!`. Dettagli in
"Sessione 23".

**Dalla sessione 25 il caricamento finisce come deve.** Il walker era
innocente: due thread del titolo scrivevano nello stesso command buffer perche'
`GameInit` non arrivava a fermare il thread della moneta -- un parser di testo
aveva uno switch che il lifter non risolveva (la ricerca della base della
tabella si fermava a un `blr` intermedio) e ogni giro del ciclo di parsing
diventava una chiamata annidata, fino al cap di ricorsione del runtime. Fix
nel lifter (`tools/ppu_lifter.py`): 107 switch in piu' risolti in tutto l'ELF,
`Audio prototypes loaded; 2429 total`, il thread della moneta esce, "segmento
senza uscita" e "run out of memory" a zero. Ora il main loop gira a ~30
fotogrammi/s ma vuoti (stato NDI/menu): e' il fronte della sessione 26.
Dettagli in "Sessione 25".

**Dalla sessione 26 il gioco arriva al menu principale.** I fotogrammi vuoti
erano un'attesa di input (`LBP_AUTOPRESS=1` preme CROSS/START); poi il
filmato dei loghi (cellVdec riscritto con l'API vera, immagini nere), i
moduli SPU dei job riliftati con il ritorno via registro (`ret_link`), e il
titolo carica il menu e prefetcha il livello `jungle-a-1`. Nel menu pero'
disegna zero e non reagisce ai tasti: i materiali del menu falliscono la
registrazione ("Technique index 0 already exists") e `cellSaveDataListAutoLoad`
manca. Dettagli in "Sessione 26".

**Dalla sessione 27 il main loop disegna.** In tutti gli stati del main loop
(schermata autosave, NDI, menu) i fotogrammi erano VUOTI (0 draw): il
renderer del fotogramma (`func_00092C48`, chiamato da `BigLoop`) usciva
subito perche' il controllo "la matrice della camera ha NaN?"
(`func_0015E308`, `vcmpeqfp.` + `blt cr6`) leggeva un CR6 mai scritto: il
lifter non aggiornava CR6 per le forme con punto dei confronti vettoriali
(`vcmpeqfp.`, `vcmpgefp.`, `vcmpgtfp.`, `vcmpequ{b,h,w}.`, `vcmpbfp.`).
Corretto in `tools/ppu_lifter.py`: da 0 a **~12 draw per fotogramma** dalla
schermata autosave in poi (passaggi fuori schermo + 4 sul display). L'immagine
e' ancora un fotogramma verde uniforme: tutti i vertici escono dal clip con
w negativo (x[-3.4 -2.0] y[-6.7 -5.5] w[-4.0 -2.7]) -- e' il fronte aperto.
Gli errori "Technique index 0 already exists" sono dati veri del titolo
(maschere di tag identiche in memoria, lette con `PS3_TTY_DUMP`), non un
difetto nostro.

**Sempre nella sessione 27, il menu principale e' sullo schermo.** Ci sono
voluti, in fila: il vertex program del guest tradotto in MSL nel backend
Metal (prima c'era solo una MVP fissa dalle costanti 0..3), gli indici letti
dal registro giusto (SET_INDEX_ARRAY_DMA, non dall'offset), le texture legate
per draw (prima tutti i draw del fotogramma usavano le ultime), un secondo
bug del lifter SPU (l'idioma `brsl $r2,.+4; ai $r2,$r2,8; bid $r2` letto come
ritorno: il job di culling usciva dal suo helper DMA senza fare la DMA), tre
istruzioni SPU mancanti nel disassemblatore (`orbi/xorhi/xorbi`), lo script
`relift_spu.sh` che liftava il policy module senza gli ingressi extra (un
commento dentro il comando), il walker della FIFO che deve ASPETTARE sui
JUMP-a-se-stessi dei buffer scritti dai job SPU (`GCM_PARK_WAIT=1`) e
attraversare gli zeri (NOP) dentro le CALL, e lo slot di partenza del vertex
program (`SET_TRANSFORM_PROGRAM_START`). Risultato: ~33 draw per fotogramma
nel menu, testo (glifi sbagliati), l'icona del tasto X, la voce evidenziata
in giallo. Manca lo sfondo 3D (i buchi dei draw nei buffer SPU restano NOP)
e i glifi del testo sono quelli sbagliati. Dettagli in "Sessione 27".

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

## Sessione 27 — il main loop non disegnava: CR6 mai scritto dai confronti vettoriali

### Il percorso

1. `RSX_FRAMESTAT=30`: nel main loop 0 draw in TUTTI gli stati (autosave,
   NDI, menu); i draw visti finora erano la moneta (thread "Init") e il
   filmato (player MPEG). Quindi il problema non era del menu.
2. Il menu si disegna sul PPU in modalita' immediata (`GAMEMENU::Drawer::
   PushToIce` 0x63E928 -> `menudrawer DrawQuad()` 0x63ED20: Begin(QUADS),
   4 vertici, End). Catena diretta da `BigLoop` (0x4D238): 0x92C48 ->
   0x64B1F8 -> 0x643240 -> 0x63E288 -> 0x63DE08 -> `DrawerItem::Draw`
   (callgraph: `tools/callgraph.py recomp --why func_0063DE08`).
3. `FLOW_WVAL` con gli indirizzi di ritorno lungo la catena (ora con tetto
   per valore e armato da `PS3_TTY_BT`): `BigLoop` chiama 0x92C48 ogni
   fotogramma, ma 0x92C48 non arriva mai a 0x93444 (menu) ne' a 0x939CC
   (pipeline 3D). Bisezione: esce a 0x930C0 dopo `bl 0x15E308(camera)`.
4. `func_0015E308` = "la matrice della camera (+0x190..+0x1CF) contiene NaN?":
   `vspltw` + `vcmpeqfp. v0,v0,v0` + `blt cr6`. La matrice e' sana
   (`PS3_TTY_DUMP=...>1DBC+190:0x40`), ma il C liftato di `vcmpeqfp.`
   calcolava solo la maschera e NON scriveva CR6: il `blt cr6` leggeva un
   valore stantio (0) = "c'e' un NaN" = niente rendering.

### La correzione (`tools/ppu_lifter.py`)

Le forme con punto di `vcmpeqfp/vcmpgefp/vcmpgtfp`, `vcmpequb/h/w` e
`vcmpbfp` ora scrivono CR6 (8 = tutte le corsie vere, 2 = tutte false;
`vcmpbfp.`: 2 = tutte dentro i limiti), come gia' faceva la famiglia
`vcmpgt{s,u}{b,h,w}`. `vcmpbfp` leggeva anche i float senza byte-swap
(`float*` sul register file big-endian): ora passa da `ppu_vldf4/ppu_vstu4`.
315 siti nel solo chunk 000. Rilift completo + `build.sh` (~2 min).
`tools/test_ppu_lift.py` e `check_ppu_scaffold.py` verdi.

### Misura

`RSX_FRAMESTAT=30`, per 30 fotogrammi: prima `0 0 0 ...` dal fotogramma
240 in poi; dopo `343 324 360 360 ...` (~12 draw/fotogramma, ~20 fotogrammi
non vuoti su 30). `RSX_FRAMEDBG=1` al fotogramma 300 (schermata autosave):

```
superfici: 0x2D50000(1) 0x16D0000(7) 0x7D0000(1) 0x33C0000(1) 0x31E0000(3) 0x3C30000(1) 0x50000(4)  [6 fuori schermo] [display]
draws=18 (degeneri=15) verts=768 dentro-clip=0  x[-3.36 -1.96] y[-6.67 -5.54] z[-4.46 -3.21] w[-3.98 -2.73]
```

Sono DRAW_ARRAYS/INDEX (non inline): la pipeline 3D gira (passaggi di post
processing con texture 2560x720 fmt 0xA5, 1280x720 0xBA). Ma **nessun
vertice dentro il clip, w negativo**: il fotogramma salvato
(`RSX_DUMP_FRAME=5`) e' verde uniforme. Prossimo fronte: la trasformazione
dei vertici (vertex program / costanti / formato degli attributi).

### Altro trovato

- "Technique index 0 already exists": con `PS3_TTY_DUMP` letti i tre
  technique di `General/menu/menu-logo/Default` in memoria: maschera tag
  `0x2` (solo "Standard") in tutti e tre, e per `Objects/hero/Hair` i
  technique 4 e 6 hanno entrambi `0x2000` ("Shadows") e lo stesso +8
  (`0x00040001`). Sono i dati del pak: il titolo stampa quell'errore anche
  su PS3. `PushToIce` usa `techniques[0]`, che si registra: innocuo.
- Assert del titolo (non deterministico, 2 corse su 4):
  `joblist.cpp(578): pJobHeader->m_jobHeaderCommand == kNoJobYet` sul thread
  14 `syn_tick_timer_thread` (audio, job list ad anello da 24 slot, wid 7).
  Il PPU trova uno slot non ancora "kNoJobYet" quando accoda: il job manager
  SPU non ha consumato/azzerato lo slot in tempo, o la lista e' piena. Il
  runtime prosegue dopo il trap. Da guardare se l'audio dara' problemi.
- Nel menu girano solo due moduli SPU per fotogramma: ovl 227
  (`jobmod_00931cc0`, sintetizzatore audio: stringhe `polarpan.cpp`,
  `SYN_ASSERT`) e ovl 212 (`jobmod_008cc900`, "CHANCE ASSERTION").
  Il rendering della scena non passa da job SPU: e' PPU (DRAW_ARRAYS).

### Seconda parte: dal fotogramma verde al menu (in ordine di scoperta)

1. **Vertex program del guest in Metal.** `rsx_vp_decompile_msl()` (nuovo in
   `rsx_vp_decompiler.c`: stesso corpo dell'HLSL con le grafie MSL, input
   compatti `[[attribute(N)]]` per i soli attributi letti, costanti +
   viewport in `constant VPConst& [[buffer(0)]]`, PSInput del fragment
   decompiler). Nel backend: cache dei VP per hash (`s_vps`), flusso di
   attributi a passo variabile letto sul CPU (`rsx_fetch_attrib`, come il
   D3D12) in `s_vpd` (64 MB), blocchi di 512 costanti + scale/offset per
   draw deduplicati (`s_cbs`), libreria (VP, FP) e PSO per VP. `RSX_VP=0`
   torna al percorso incorporato. I quad 2D del titolo: posizione da attr1,
   `x' = 2x-1, y' = -2y+1` con c256/c257, z da c467.
2. **Indici.** `resolve_indexed` leggeva il tipo dall'OFFSET e risolveva
   con la tabella IO anche la VRAM: ogni indice era 0 (tutti i quad in un
   punto). Ora da `index_array_dma` (tipo bit 4-7, locazione bit 0-3), come
   `read_guest_index` del D3D12.
3. **Texture per draw.** `MtlDraw.tex[16]` (puntatori non ritenuti) presi
   da `s_tex` al momento del draw; `s_texcache` (2048 voci per offset/
   formato/dimensioni, evizione a fine fotogramma dopo 600 fotogrammi).
4. **Nel menu il main thread si bloccava** dentro `CreateVisibleBuckets`
   (0x185360) -> API WwsJob (0x74xxxx) aspettando i job di culling
   (`sample` del processo: spin in 0x742838/0x74073C). Sul lato SPU
   `SPURS_PM_WATCHDOG=5`: corsa di wid=10 ferma a 0x81C4 dopo tre
   `stopd` a 0xB3CC/0xAE60/0xB3F4 (ovl 204 = `jobmod_00894700`,
   `game/render/culling.cpp`). L'assert a 0xAB3C e' `rdch SPU_RdMachStat;
   andi 1; brz` = "gli interrupt devono essere abilitati"; erano spenti
   perche' l'helper DMA 0xB948 usa l'idioma
   `brsl $r2,.+4 ; ai $r2,$r2,8 ; bid $r2` (salta a .+12 disabilitando gli
   interrupt) e `compute_link_returns` (`tools/spu_lifter.py`) vedeva
   `bi $r2` con r2 "mai scritto nella funzione" -- l'`ai` sta nella funzione
   aperta dal target del brsl -- e lo emetteva come `SPU_RET`: l'helper
   tornava al chiamante senza fare la DMA. Ora la scansione all'indietro e'
   lineare (si ferma ai flow break, non ai confini di funzione).
5. `relift_spu.sh`: il blocco di commenti stava DENTRO il comando del policy
   module e ne spezzava la continuazione: il PM veniva liftato senza
   `--extra-funcs/--force-indirect` (il lift buono era stato fatto a mano).
6. `tools/spu_disasm.py`: mancavano `orbi` (op8 0x06), `xorhi` (0x45),
   `xorbi` (0x46): il job di geometria (ovl 215, `jobmod_008dc900`, 74 KB)
   eseguiva `.word 0x06004716` a 0xE160. Gli altri "unsupported" dei moduli
   sono stringhe/tabelle nel segmento codice, mai eseguite.
7. **Buffer di comandi dei job SPU.** L'anello fa `CALL 0x0A400000` (IO ->
   EA 0x20500000, mappatura del titolo); il buffer comincia con
   `2A400000` = JUMP a se stesso, scritto dal job insieme a 22 NOP
   (`40580100`), e viene PATCHATO dopo (DMA di 0x50 byte da pc 0x41B0:
   `00041710 0 | 00041680 8AF99980 | 000416A0.. | 0004168C.. | 0004181C.. |
   00041820 00000011 | 8 zeri | 00041D6C 490 | 00041D74 20999A00 | 00020000
   RET`). La RSX vera aspetta sul park; il walker invece risincronizzava
   sull'anello ("jump-park"), saltava il buffer e la SET_REFERENCE dietro la
   CALL: "Rendering is stuck on reference value 645". Ora `GCM_PARK_WAIT=1`
   (in `run.sh`): resta parcheggiato e il ticker del present ripassa ogni
   pochi ms anche con `GCM_DRAIN_ONLY_PUT`. E dentro una CALL gli zeri sono
   NOP da attraversare (la regola dei 4 zeri era per l'anello).
8. **`SET_TRANSFORM_PROGRAM_START`.** La RSX ha 512 slot di VP e il titolo
   ne tiene caricati piu' d'uno: hash e decompilazione partono dallo slot di
   inizio. Prima i quad del testo (VP 91A6DB47, 9 istruzioni, attr 0/2/3/4)
   usavano il VP a slot 0 e sparivano.

Con tutto questo (`run_s27_c30.log`, `RSX_DRAWLIST=900`): 33 draw per
fotogramma nel menu -- 14 passaggi di post-processing a catena (1280x720 ->
320x180 -> 160x90 -> 40x23 -> 10x6 -> 3x2 -> 1x1: l'esposizione), il
composito sul display, 13 draw dell'interfaccia con blend (VP 91A6DB47, FP
0x0F900401, texture 256x128 e 512x512). Il fotogramma (`RSX_DUMP_FRAME=25
RSX_DUMP_SKIP=60`) mostra il menu: righe di testo con i glifi sbagliati, il
tasto X in un cerchio blu, una voce in giallo. Sfondo nero: i buchi da 8
parole nei buffer SPU (dove il job di geometria patcha i draw) restano zeri.

### Il prossimo passo

1. I glifi: il testo usa attr 0/2/3/4 dai vertici immediati; da capire se
   sono le coordinate di texture (attributo sbagliato) o la texture stessa
   (formato dell'atlante 256x128). `RSX_VP_DBG` con `RSX_VP_DBG_FP=0F900401`
   sulla via inline (oggi stampa solo record_draw).
2. Lo sfondo 3D: i buchi restano NOP. Chi dovrebbe patcharli (job di
   geometria ovl 215?) e perche' non lo fa: `SPU_DMA_EAWIN` sui buffer
   (EA 0x20500000+), `SPU_JOBTRACE_OVL=215`.
3. Il menu reagisce ai tasti? Con l'immagine si puo' vedere.
4. Assert non deterministico `joblist.cpp(578)` sul thread audio (tid 14).

### Strumenti nuovi

- `RSX_VP_DUMP=1` (MSL del VP tradotto), `RSX_VP_DBG=<n>` [+
  `RSX_VP_DBG_FP=<hex>`, armato da `PS3_TTY_BT`]: attributi dei primi due
  vertici, costanti c0-3/c256-259/c467, viewport, formato degli attributi,
  indici risolti e byte grezzi. `RSX_DRAWLIST=<frame|0>`: l'elenco dei draw
  di un fotogramma (superficie, VP, FP, vertici, blend, ritaglio, texture).
  `RSX_DEBUG_TINT=1|2` ora vale anche con il VP del guest (2 = solo i draw
  con blend). `GCM_PARK_WAIT`, `SPU_JOBTRACE_OVL=<id>`, `ovl=` anche in
  `[spu-assert]` e `[dma-eawin]`.
- `PS3_TTY_DUMP=<spec>[,<spec>]` con `PS3_TTY_BT`: dump di memoria guest a
  ogni hit. spec = `addr[>off|+off]*:len`; `>off` fa a = read32(a+off),
  `+off` somma. Es. `0x7CB238>0>-7D48>1DBC+190:0x40` = matrice della camera
  (r30 di BigLoop = *(0x7CB238); G = *(r30-0x7D48); camera = G->0x1DBC).
- `PS3_CALLTRACE` ora si arma con `PS3_TTY_BT` (parte dall'hit) e filtra
  per thread con `PS3_CALLTRACE_TID=<tid>`.
- `FLOW_WVAL` (64 bit): tetto di 24 stampe PER VALORE e armato da
  `PS3_TTY_BT`. Con gli indirizzi di ritorno (lr salvato nel prologo) e'
  una bisezione a costo zero di "fin dove arriva questa funzione".
- `SPU_JOBTRACE=1`: armato da `PS3_TTY_BT` e con `ovl=<id>` (quale modulo
  di job gira; id -> nome in `gen/jobmods_register.c`).
- `RSX_DUMP_SKIP=<m>`: salta i primi m fotogrammi sopra soglia prima di
  salvare (con `RSX_DUMP_FRAME`/`RSX_DUMP_COUNT`), per fotografare il menu.
- Nota: `build.sh` NON ricompila il runtime (`runtime/`, `libs/`): quello
  sta in `build/libps3recomp_runtime.a` e vuole `cmake --build build`
  prima. Solo `ppu_loader/ppu_hle/ppu_fs/ppu_sysprx/boot_main` sono
  compilati da `build.sh`.

## Sessione 26 — dal caricamento al menu principale: il gioco aspettava un tasto

### 1. I fotogrammi vuoti erano un'attesa di input

`LBP_AUTOPRESS=1` (impulsi di CROSS e START ogni 240 letture del pad, gia'
nel runtime) sblocca lo stato "main-menu-ndi": il titolo avvia il filmato dei
loghi (`starting movie sce-ndi-logos`, `movie1/sce-ndi-logos.m2v`, MPEG-2 via
libvdec) e disegna i primi quad veri (`DRAW_INDEX_ARRAY` con texture 1280x720
fmt 0xA5). Nessuno premeva niente: da qui in poi le corse vanno fatte con
`LBP_AUTOPRESS=1`.

### 2. cellVdec: riscritto con l'API vera

`libs/codec/cellVdec.{c,h}` avevano firme e layout inventati (Open con
cbFunc/cbArg separati invece del puntatore a `CellVdecCb`, GetPicture con
`picItem**`, msgType AUDONE=0, codec MPEG2=2...) e chiamavano la callback
guest come puntatore host: crash `UNTRANSLATED GUEST POINTER 0x0FEFF970` alla
prima DecodeAu. Ora: layout RPCS3 (documentati nell'header), callback via
`ps3_invoke_guest` (sincrona, dentro DecodeAu/EndSeq), `cellVdecQueryAttr`
(NID 0xFF6F6EBE, era non registrato), PicItem + Mpeg2Info scritti nella
memoria della `CellVdecResource` del titolo (0x2B7C8000+0x10000), coda FIFO di
256 descrittori (il titolo manda ~110 AU di fila e poi ritira con
GetPicItem una per una: con un solo slot girava su GetPicItem per sempre --
misurato con `PS3_HLE_TID_TRACE`: 3785 chiamate), dimensione dal sequence
header MPEG-2 (1280x720), immagine nera nel formato chiesto (fmt 0 =
ARGB32, scritta direttamente in VRAM 0xC16D0000). Il filmato arriva a
`Close`, poi il titolo carica il **menu** (`'menu': loaded 20.04 Mb`,
`main-menu-menu`) e prefetcha `jungle-a-1` e `dive-boat-movie-intro`.

### 3. I moduli SPU dei job erano liftati senza SPU_RETLINK

`[SPU] BRANCH-TO-0 unresolved pc=0x0D3F0 image=2` + `[spu-smc] microstep
UNKNOWN word 0x3F830484` (un `rotqbyi`): il job `jobmod_00931cc0` a LS 0xD3C0
chiama 0xEE00, che rientra con `bi $r39` = ritorno via registro; il modulo era
stato liftato PRIMA che il lifter salvasse `ret_link` attorno alle chiamate
(0 siti nei 28 moduli contro 85 nelle immagini spu_lifted), quindi il ritorno
diventava un salto indiretto a un indirizzo che non e' un ingresso e finiva
nell'interprete di ripiego. Rilift dei 28 moduli con `tools/lift_jobmods.py`
(1765 siti ret_link): zero `BRANCH-TO-0`, zero `wws-jobentry`.

### Strumenti nuovi

- `PS3_HLE_TID_TRACE=<tid guest>` + `PS3_TTY_BT='testo'`: da quando il titolo
  stampa quel testo, ogni import HLE chiamato da quel thread (nome, lr,
  r3-r5; max 4000 righe). E' quello che ha mostrato il ciclo su GetPicItem.
- `RSX_FRAMESTAT=<n>` conta i draw negli ultimi n fotogrammi presentati.

### Dove si ferma adesso

Nel menu principale: ~60 fotogrammi/s **vuoti** (0 draw, nessuna CALL), il
titolo legge il pad a ogni frame (`cellPadGetData` una volta per frame) ma non
reagisce agli impulsi di CROSS/START per minuti. Due sospetti:

1. `ERROR: Technique N of material 'General/menu/...': Technique index 0
   already exists` (26 volte, tutti i materiali del menu: menu-logo,
   shot-chapters, shot-movies, menu-map...): la registrazione delle tecniche
   (`func_00483F90`, test di bit su una maschera a 64 bit letta con
   `vm_read64` e `srd`) assegna indice 0 a tutte le tecniche. Se i materiali
   del menu non si registrano, il menu non ha niente da disegnare e forse il
   suo script aspetta qualcosa che non arriva. Appare anche una volta durante
   GameInit. Da capire se e' un errore di lift (bitfield 64 bit) o di dati.
2. ~~`cellSaveDataListAutoLoad` mancante~~ -- implementata (fine sessione)
   con callback guest via `ps3_invoke_guest`: funcFixed del titolo risponde
   result=0, dirName='NOTE_FREE_SPACE', noi rispondiamo NODATA, il thread
   "Save/Load Game Thread" termina pulito. **Il menu resta identico**: non
   era lui. (Le altre funzioni di `cellSaveData.c` chiamano ancora le callback
   come puntatori host: da rifare quando il titolo le usera'.)

### Il prossimo passo

1. Capire la tecnica con indice 0 (leggere `func_00483F90` intorno a
   0x484260-0x484408 con il disassemblato originale, `show_func.py --asm`:
   e' una generazione di nomi di tecnica guidata da bit di feature in una
   maschera a 64 bit; `rldicl`/`srd` liftati sono corretti, quindi il sospetto
   passa ai DATI del materiale o a una stringa costruita male).
2. Capire cosa aspetta lo script del menu (`main-menu-menu`): gli impulsi
   CROSS/START arrivano (`cellPadGetData` una volta per frame, injection nel
   runtime) ma niente cambia per 150 s.
3. Se il menu reagisce all'input: `dive-boat-movie-intro` e il livello
   `jungle-a-1` sono gia' in prefetch -- il gioco vero e' dietro l'angolo.
4. Ripristinare gli screenshot (la finestra non si trova da questa shell).


## Sessione 25 — il walker era innocente: due thread scrivevano nello stesso command buffer, per un salto mal liftato

(La sessione 24 ha prodotto solo strumenti e 14 corse, senza note: qui c'e' anche
quello che ha lasciato in sospeso.)

### 1. Cosa faceva davvero il walker

Con `GCM_SEQ=1` + tid guest nei `[SEQ]` e `GCM_RINGDBG=1` (che ora stampa anche
il command context del titolo, `C = *(*(toc-0x7DB8) - 0x7FB4)` = 0x1068B1A4:
begin/end/current/callback, i due buffer IO 0x660000/0x860000, frame index):

- Il titolo ha UN solo context, doppio buffer per fotogramma; `SwapBuffers`
  (0x14F528) scrive nello slot dell'anello `[SET_REF n][JUMP -> begin(buffer)]`
  e al `C.current` il JUMP di coda verso lo slot successivo (0x704F30), poi
  muove `put`. `cellGcm_on_put_write` cammina tutto in modo sincrono: se le
  scritture precedono `put`, il walker le vede. Tutto corretto.
- Il "segmento senza uscita" (zeri a 0x660930/0x860930) nasce perche' **due
  thread guest emettono nello stesso context nello stesso momento**: tid 4
  ("Init", prio 30) disegna la moneta (`func_0004C880`, 566 parole) e tid 1
  (main) disegna il suo fotogramma (`func_0004D238`). Il secondo sovrascrive
  il JUMP di coda del primo con i propri comandi, e il walker arriva alla
  testa di scrittura. Le 12.633 draw e i blocchi colorati sono lo stesso
  caos. Sull'hardware vero non succede: il ciclo della moneta esce quando il
  main gli scrive il flag `0x1018B019` (fine di `GameInit` 0x4E568, a 0x4F7C4)
  e poi aspetta `0x100A15EC == 0` prima di partire col main loop.

### 2. Perche' il main loop partiva senza chiudere la moneta

`GameInit` non arrivava a 0x4F7C4: fra `lr 0x4F108` e `lr 0x4F124` (parsing di
`sound1/config/prototypes.txt`) il log dice
`[ppu] recursion cap @0x0068E110 depth=4001 -- skipping`, poi
`Audio prototypes loaded; 0 total.` e una riga `Global, ...` con il nome file
sostituito da un pezzo del testo dei prototipi. Da li' il puntatore di stack
guest r1 resta spostato di -0x2D90 (misurato con `FLOW_WVAL` sugli indirizzi
di ritorno salvati: i callee di GameInit salvano lr a 0x0FEFF9E0 prima e a
0x0FEFCC50 dopo), GameInit legge i propri locali dal posto sbagliato e
"ritorna" a `func_000EAD40` con r1 falso; il main loop parte con la moneta
ancora viva.

**La causa e' nel lifter.** `func_0068D918` (parser di testo, 2842 istruzioni,
frame 0x1C70) ha uno switch a 0x68E134 con tabella di offset a due livelli
(`lwz r11,-0x7F5C(r30)`, r30 = `lwz r30,-0x755C(r2)` nel prologo a 0x68D924).
`discover_jump_tables` cercava la definizione di r30 all'indietro fino al
`blr` piu' vicino -- e c'e' un `blr` (return anticipato) a 0x68DB08, prima del
prologo. Tabella non risolta -> il `bctr` diventa `ps3_indirect_call` -> ogni
`b 0x68E114` (torna in cima al ciclo di parsing) e' una chiamata annidata ->
4001 livelli -> il cap del runtime salta la chiamata. Fix in
`tools/ppu_lifter.py`: la ricerca si ferma al prologo (`stdu r1,-N(r1)`) invece
che al `blr`, con il `blr` come ripiego se non c'e' prologo entro 8000
istruzioni. **Esito del rilift: dispatcher risolti 138 -> 245 in tutto l'ELF; `recursion
cap` sparito; `Audio prototypes loaded; 2429 total` (era 0); GameInit scrive
0x1018B019, il thread della moneta esce (`sys_ppu_thread_exit(tid=4)`), poi
parte il main loop. In 3 corse da 100-180 s: "segmento senza uscita" 0 (era
1000-2000), "Command context has run out of memory" 0, "Rendering is stuck" 0.**

### 3. Nota: cellDiscGameGetBootDiscInfo

`libs/system/cellSysutil.c` scrive il titleId nella struct del titolo PRIMA di
decidere se rispondere NOT_DISCBOOT: con `PS3_DISCGAME_BOOT=0` il titolo si
troverebbe comunque "BLES00000" a 0x10189BA8. Non e' la causa di niente qui
(il PARAM.SFO dell'ISO dice CATEGORY=DG, TITLE_ID=BCES00065: e' il disco
retail europeo, non la demo NPUA98103), ma RPCS3 non scrive nulla in quel
caso. Da sistemare quando serve.

### Strumenti nuovi (sessione 24+25)

- `GCM_RINGDBG=1`: a ogni mossa di `put`, slot dell'anello, coda dei segmenti,
  command context del titolo, flag della transizione caricamento->gioco
  (quit 0x101879C5, abort 0x10189BA8, stop 0x1018B018, exit-ok 0x1018B019,
  coin-on 0x100A15EC).
- `GCM_PUT_BT=lo:hi`: catena guest (con tid) di chi muove `put` con ref in
  [lo,hi]. `PS3_TTY_BT='a|b|c'`: catena guest quando il titolo stampa una di
  quelle sottostringhe. `[GCS:...]` ora porta `tid=`.
- `YDKJ_WWATCH=a1,a2,...` (fino a 8 finestre) e ora vede anche gli store a
  byte. `FLOW_WVAL=v1,v2,...`: piu' valori; usato per seguire gli lr salvati
  nei prologhi = "quali chiamate sono avvenute e con quale r1".
- `GCM_SEQ_RAW=1`, `GCM_JMPWATCH`, `GCM_STRAND_LATE=1` (sessione 24).
- `scratchpad/runfor.sh <s> <log> [VAR=..]`: lancia, aspetta, `kill -9`,
  verifica. **Il port ignora SIGTERM**: `kill`/`pkill` semplici lasciano il
  processo vivo (16 orfani trovati a meta' sessione, tutte le misure di quel
  tratto erano a macchina piena; le conclusioni strutturali sono state
  riverificate a macchina pulita).

### Il fronte ora: il main loop presenta fotogrammi vuoti

Dopo l'uscita della moneta (~10 s dall'avvio) il main loop emette ~30
fotogrammi/s tutti uguali (`GCM_SEGDUMP=300`): superficie 1280x720, clear,
viewport, scissor, i 5 semafori, JUMP -- **nessun draw e nessuna CALL** a
display list. `RSX_FRAMESTAT=300`: "0 draw in 0 fotogrammi non vuoti" per
tutto il resto della corsa. Il titolo e' nello stato "main-menu-ndi" (task
prefetch `main-menu-autosave-warning`, `main-menu-ndi`, audio
`sce-ndi-logos.xvag`), non stampa errori, e il thread principale ogni tanto
gira su 0x20000000 (`[HOTREAD]`, attesa del job manager, 48 volte in 150 s:
periodica, non permanente). Ipotesi da verificare, in ordine: (1) aspetta un
input (X/START) che nessuno preme; (2) aspetta lo stream audio/xvag o un
movie (VDEC caricato, `movie1/` letta); (3) un job SPU che non consegna.
`GCM_PUT_BT` con ref alto e `PS3_TTY_BT` restano gli strumenti giusti.

Nota: lo screenshot via `CGWindowListCopyWindowInfo` da questa shell non vede
la finestra (permessi); i `RSX_DUMP_FRAME=1` funzionano (frame della moneta a
2 draw). Da ripristinare un modo per fotografare lo schermo.

### Il prossimo passo

1. Capire cosa aspetta il main loop nello stato NDI/menu (sopra).
2. Poi ricontrollare draw/fotogramma e la texture 0x06312F00 (sessione 23),
   che ora dovrebbero essere un problema diverso o sparito.


## Sessione 23 — lo stallo intermittente erano due tabelle da 8 slot nel runtime SPU

### 0. La "striscia" della sessione 22 non era un difetto

Il "prossimo passo" numero 1 della sessione 22 (l'oggetto dorato schiacciato in
36x122) e' chiuso senza toccare codice: la texture 4096x128 legata al disegno e'
`build/main/boot1/coin.dds`, uno **sprite sheet di 32 pose da 128x128 della
moneta che gira** (la rotellina di caricamento). Misurato sull'atlante
decodificato, larghezza della moneta per posa: 123, 116, 107, 95, 78, 58,
**35, 18, 36**, 58, 79 ... — la posa 6/8 e' larga 35-36 pixel e alta 123: e'
esattamente il riquadro disegnato. La decodifica BC1 della sessione 22 e'
corretta (la moneta e' leggibile, con la scritta "PHILIPPUS ... HISPANIARUM").
Quindi: il titolo era sulla **schermata di caricamento**, in basso a destra.

`RSX_DUMP_COUNT=<k>` (nuovo) salva i primi k fotogrammi sopra soglia in
`/tmp/rsx_frame_<i>.ppm`, per vedere una sequenza e non un solo istante.

### 1. Il pad: il layout di `CellPadInfo` era sbagliato, non la logica

`Pad 0 has been removed!` continuava a comparire anche dopo la correzione della
sessione 22. Letto il codice del titolo (`func 0xCD98C`, il poll del pad):
dopo `cellPadGetInfo` legge lo stato della porta a **`info+0x208+i`**
(`lbz r28, 0x208(r9)`), non a `+0x28`. La v1 dell'API ha gli array da
`CELL_MAX_PADS = 127` elementi (RPCS3 `cellPad.h`, PSL1GHT `padInfo`):
`vendor_id[127]` a +0x0C, `product_id[127]` a +0x10A, `status[127]` a +0x208.
La nostra HLE scriveva gli array da 7 (il layout di `CellPadInfo2`), e il
titolo leggeva lo stack: a volte 0 ("removed"), a volte spazzatura
("connected"). Ora il layout e' quello vero: **0 "removed" in 9 corse**.

Con il pad visto, il titolo chiama tre import che non avevamo —
`cellPadInfoSensorMode` (0x78200559), `cellPadSetSensorMode` (0xBE5BE3BA),
`cellPadSetPressMode` (0xF83F8182) — piu' `cellPadInfoPressMode`, e a ogni
fotogramma `cellKbRead` (0xFF0A21B7: il nome SDK di quello che qui si chiamava
`cellKbGetData`). Aggiunti tutti; la tabella NID va rigenerata
(`tools/gen_hle_nids.py --all --out ~/Documents/Coding/uncharted-data/gen/ppu_hle_nids.cpp`).

### 2. Lo stallo "modo A": misurato, non piu' indovinato

Con il pad corretto lo stallo dopo il caricamento e' diventato **sistematico**
(4 corse su 4 da 240 s: `spinning on 0x20000000` da `lr=0x532620`), quindi
finalmente misurabile. Strumento nuovo: **`SPURS_PM_WATCHDOG=<s>`** stampa
ogni corsa del policy module che non torna entro s secondi, con wid, lane, pc,
gli ultimi 16 salti dispacciati (anello per contesto, `wd_ring`) e lo stato MFC
(`ie`, maschera/stato eventi, `tagmask`, tag completati, liste parcheggiate).

Prima misura: la lane ferma cicla in 0x2430/0x2438/0x2448 del PM — la routine
che aspetta i tag DMA — con **`tagmask=0x80000000 completati=0x7FFFFFFF`**:
il tag 31 non si completa mai e nessuna lista e' parcheggiata. Il tag non e'
in volo da nessuna parte: e' andato perso.

**Causa 1 — `SPU_MAX_CONTEXTS = 8` in `spu_channels.c`.** I motori MFC
(`tag_completed`, coda) erano 8 slot; ogni contesto oltre l'ottavo finiva su
**un unico motore condiviso** ("correct for single-SPU"). Uncharted ha 9
workload WwsJob con `maxContention` 5-6, cioe' fino a 54 contesti persistenti
(`s_pm_ctx[wid][spu]`), eseguiti su thread host concorrenti: due lane facevano
`tag_completed &= ~bit` / `|= bit` sulla stessa parola, e una scrittura persa
lasciava un tag incompleto per sempre. Ora: 256 slot, assegnazione sotto lock
(`resv_lock`), puntatore in cache nel contesto (`mfc_cache`), e se gli slot
finissero lo dice a voce (`[spu-mfc] finiti gli slot`).

Seconda misura, dopo la correzione: ancora una lane ferma, ma diversa —
wid=4 lane 0 cicla fra 0x2528, 0xA90 (la coda del gestore di interrupt di
WwsJob: rigenera lo shim a 0x3FEC0, ripristina r76-r79, `iret`) e 0x2E28
(`bi $r0` con r0=0x2528), 21,8 milioni di salti al secondo, `ie=0`,
`completati=0xFFFFFFFF`. Un `iret` che torna in un punto che rientra subito
nel percorso di uscita dall'interrupt: registri sbagliati al ritorno.

**Causa 2 — `g_irq_save[8]` in `spu_drain.c`.** Il salvataggio del file
registri all'ingresso di un interrupt (contratto hardware: il gestore
preserva tutto) era una tabella globale di 8 slot per puntatore di contesto,
senza lock: oltre l'ottavo contesto "behave as before" (nessun ripristino), e
due thread potevano prendersi lo stesso slot. Ora vive nel contesto
(`irq_save_valid`, `irq_resume_pc`, `irq_gpr[128]`).

| | prima (s1-s4) | solo MFC (m1) | entrambe (m2, m3, b1, b2, b3) |
|---|---|---|---|
| corse da 240 s | 4 | 1 | 5 |
| stallo (spin permanente su 0x20000000) | **4/4** | 1/1 | **0/5** |
| corse del PM segnalate dal watchdog | 2-5 per corsa | 1 | 0 |

(m1 e' partita col binario prima della causa 2; m2 e' partita a cavallo della
ricompilazione — il conteggio prudente e' 5 corse pulite, non 6.)

### 3. Il fronte ora: il walker della FIFO RSX

Senza stallo, il titolo va oltre e stampa un errore suo:
**`ERROR: ERROR: Command context has run out of memory!`** (in 5 corse su 5,
fra la riga 22k e la 117k). Prima di quello, i fotogrammi pieni hanno sempre
12.633 draw e mostrano il riquadro della moneta pieno di **blocchi colorati**:
la texture legata e' 0x06312F00, dichiarata 4096x128 DXT1 come l'atlante ma con
contenuto che non lo e' (`RSX_TEX_SAVE`). Ipotesi: uno stato dei registri
texture non aggiornato perche' il walker perde o ripete comandi.

Misurato con `GCM_RECDBG=1`, `GCM_STRAND_PEEK=1` (nuovo: 64 parole prima e 16
dopo il punto di arenamento) e `YDKJ_WWATCH=0x21660930` (IO 0x660930; l'IO
parte a EA 0x21000000, `Init(ioSize=0x1300000, ioAddr=0x21000000)`):

- L'anello e' a 4 slot da 0x1000 (IO 0x10000..0x13000): `[SET_REFERENCE]
  [valore] [JUMP -> segmento]`, `put` posato a slot+8. Due segmenti, 0x660000 e
  0x860000, usati **due slot ciascuno** per giro.
- Il segmento 0x860000 finisce con `JUMP -> 0x10000` (a 0x8608D8). Il segmento
  0x660000 finisce a **0x930 con zeri** quando il walker ci entra: 2.665-4.580
  recuperi "segmento senza uscita" per corsa, sempre a 0x660930/0x860930/
  0x86098C/0x660B00, cioe' sistematici, non una corsa.
- Il titolo scrive davvero a 0x660930 (`guest-fn=0x704F30`): **`0x20011000`,
  il JUMP di coda verso lo slot 0x11000**, e SUBITO DOPO lo sovrascrive con
  altri comandi (`0x00041EA8`, `SET_SEMAPHORE_OFFSET`...) da `0x703488` e
  `0x48A888`: la coda si sposta in avanti a ogni aggiunta, come annotato nella
  sessione 19.
- Con la coda a 0xB00 il walker gira in **ciclo**: `0x11008 -> 0x660000 ->
  0x660B00 -> 0x11000 -> 0x11008 ...` con `put=0x13008`, finche' il rilevatore
  di cicli (sessione 20) lo tira fuori. Cioe' il JUMP di coda punta allo slot
  che ha saltato nel segmento, non al successivo: per il titolo e' un anello di
  attesa che la RSX percorre finche' la CPU non lo ridirige.
- Il contenuto prima dello zero e' plausibile fino all'ultima parola (vertici
  in modalita' immediata della moneta: colore 4F, texcoord 2F **0.1875..0.219 =
  posa 6 dell'atlante**, posizione 2F; `SET_BEGIN_END(0)`; cinque coppie
  `SET_SEMAPHORE_OFFSET 0x480..0x4C0` + `TEXTURE_READ_SEMAPHORE_RELEASE`).

Da capire nella prossima sessione: **come la RSX vera percorre questo anello
senza ripetere i disegni** (ogni nostro rientro nel segmento ri-esegue i draw
dall'inizio: 12.633 potrebbe essere il doppio del vero) e perche' il titolo
conclude che il command context e' pieno — probabilmente legge un `get` che
non avanza come si aspetta. E' il lavoro delle sessioni 19-20 ripreso con dati
migliori, non un problema nuovo.

### Strumenti nuovi

- `SPURS_PM_WATCHDOG=<s>` — corse del PM che non tornano entro s secondi.
- `RSX_DUMP_COUNT=<k>` — piu' fotogrammi sopra soglia, numerati.
- `GCM_STRAND_PEEK=1` — parole attorno al punto di arenamento del walker.
- Il layout dei dati del titolo per queste misure: IO 0x660930 = EA 0x21660930.

### Il prossimo passo

1. Il walker della FIFO: capire l'anello di attesa del titolo (sopra) e
   sostituire i recuperi con il comportamento della RSX vera. `RPCS3` e'
   l'oracolo: come gestisce un JUMP che torna sullo slot corrente con `put`
   piu' avanti.
2. La texture 0x06312F00 "4096x128" con contenuto sbagliato — probabilmente
   cade da sola col punto 1.
3. Difetto noto del decompilatore dei fragment program (`return r[0]` vs
   `h[0]`), invariato.


## Sessione 22 — la prima immagine vera: tre difetti in fila fra i vertici e lo schermo

La sessione 21 aveva fatto arrivare i disegni al backend. Qui si e' seguito il
percorso fino al pixel, e i difetti erano **tre, in fila**: ognuno da solo
bastava a fare uno schermo nero, quindi finche' non sono caduti tutti e tre non
si vedeva niente.

### 0. Il gioco crede di non avere il controller (corretto, effetto non misurato)

Dal log del titolo: `Pad 0 has been removed!`. Il gioco interroga
`cellPadGetInfo` (v1, NID `0x3AAAD464`), ed era **l'unica delle nostre quattro
funzioni sul pad a non dichiarare la porta 0 sempre presente**:
`cellPadGetData`, `cellPadGetInfo2` e `cellPadPeriphGetInfo` lo fanno tutte
(`|| i == 0`), la v1 no. Allineata.

Onesta': **il picco di disegni che si vede dopo NON e' causato da questa
correzione** — rileggendo i log precedenti c'era gia'. La correzione resta
perche' e' un'incoerenza vera fra quattro funzioni nostre che rispondono alla
stessa domanda, ma non attribuirle meriti che non ha.

### 1. Meta' dei disegni andava fuori schermo e cancellava l'altra meta'

Con `RSX_FRAMEDBG` si vede che un fotogramma pieno ha ~10.000 disegni, e
l'istogramma delle dimensioni dice che sono **a coppie**: meta' piccoli, meta' a
schermo pieno. Quelli a schermo pieno coprivano tutto, e restava visibile solo
l'ultimo disegno del fotogramma.

Il perche', misurato registrando la superficie di destinazione per disegno:

```
[RSX frame#798]   superfici: 0x7D0000(4854) [offscreen]  0x50000(4854) [display]
```

Il titolo alterna **una superficie fuori schermo** e il buffer visibile.
`mtl_set_render_target` guardava solo la dimensione e buttava via la
destinazione: tutto finiva sul drawable.

Fatto: ogni offset di superficie non-display ha la sua `MTLTexture`, i disegni
si raggruppano per superficie in passaggi di rendering distinti (tratti
consecutivi, cioe' l'ordine in cui il titolo li ha emessi), e quando il titolo
ricampiona quella superficie come texture gli si consegna la nostra — in memoria
guest non c'e' niente, perche' a scriverci sarebbe stata la RSX.
`RSX_RTT=0` torna al comportamento di prima per il confronto.

### 2. LA CAUSA del nero: le texture legate ad array

Anche dopo la separazione delle superfici, tutto nero. La geometria pero'
rasterizzava: con una tinta fissa la finestra si riempiva. Quindi il difetto era
nella colorazione.

Il fragment program tradotto del guest dichiara le texture come **array**:

```c
fragment float4 rsx_fp_main(PSInput input [[stage_in]],
    array<texture2d<float>, 16> rsx_tex [[texture(0)]],
    array<sampler, 16> rsx_samp [[sampler(0)]])
```

e noi legavamo **solo le unita' residenti**, lasciando gli altri 14-15 slot
vuoti. Con un array, Metal in quel caso restituisce zero da *tutte* le letture,
compresa quella dello slot legato.

La prova A/B, fatta con uno shader incorporato identico a `fs_tex` salvo la
dichiarazione ad array (`fs_tex_arr`, `RSX_TEX_ARRAY=1`):

| shader | pixel non neri |
|---|---|
| `fs_tex` (dichiarazione scalare) | **2058** |
| `fs_tex_arr` (dichiarazione ad array) | **0** |

Fatto: si legano **tutti** i 16 slot a ogni disegno; quelli senza texture
residente ricevono una 1x1 trasparente (`s_dummy_tex`). Dopo la correzione il
fragment program del titolo produce colore: da 0 a 2249 pixel non neri.

### 3. Le texture compresse finivano in memoria come blocchi grezzi

A quel punto l'immagine c'era ma era **rumore rosso/verde/blu**: un riquadro di
128x32 pixel di sale e pepe saturo.

Le texture del titolo sono formato `0xA6` = `0x86` (DXT1) | `0x20` (lineare).
`rsx_texture_decode()` per un formato a blocchi **copia i blocchi cosi' come
sono** — che e' giusto per un backend che carica una texture BC nativa, come
D3D12. Il backend Metal pero' crea una `RGBA8Unorm` e ci copiava dentro i
blocchi compressi: da li' il rumore.

Fatto: `rsx_texture_decode_bc()` in `rsx_texture_layout.c`, decompressione
BC1/BC2/BC3 sulla CPU in RGBA8 (compresi l'alpha a 1 bit di BC1 quando
`c0 <= c1`, i 4 bit di BC2 e i 3 bit interpolati di BC3). Il percorso esistente
non cambia: chi carica BC nativo continua a ricevere i blocchi intatti.

### Il risultato

**La prima immagine vera del port**: un oggetto dorato con dettaglio, ombreggiatura
e riflessi, disegnato dal titolo con il proprio fragment program e la propria
texture. Sfondo `(8,4,8)`, 3592 pixel di contenuto, tinte calde — non piu' rumore
saturo.

L'oggetto esce pero' **schiacciato** in una striscia verticale di 36x122 pixel:
le proporzioni sono sbagliate, ed e' il prossimo dettaglio da capire (il quad e'
piccolo, oppure le coordinate di texture coprono solo una fetta dell'atlante
4096x128).

### Lo scissor: implementato, e non era lui

`scissor_x/y/w/h` erano tracciati in `rsx_state` e non arrivavano mai a Metal.
Ora si applicano (`RSX_SCISSOR=0` per spegnerli). Misurato pero': in un
fotogramma pieno **tutti** i 12.633 disegni hanno il ritaglio a schermo intero,
quindi non era questa la causa dei quad che coprivano tutto (era la superficie,
punto 1). Resta perche' e' corretto averlo.

### Nota di metodo, di nuovo

Il debug per fotogramma campionava una riga ogni 60, e i fotogrammi pieni
duravano meno di quello: si vedevano solo i fotogrammi vuoti. E' **lo stesso
errore del log tappato della sessione 19**. Ora un fotogramma con molti disegni
si stampa sempre. E siccome uno screenshot da fuori manca sistematicamente i
fotogrammi pieni, `RSX_DUMP_FRAME=<n>` salva su file dal di dentro il primo
fotogramma con piu' di n disegni (piu' le superfici fuori schermo).

Tetti alzati di conseguenza: `MTL_MAX_DRAWS` 4096 -> 32768 e `MTL_MAX_VERTS`
256K -> 1M, perche' a 4096 buttavamo 6.286 disegni per fotogramma, cioe' meta'
immagine.

### Strumenti nuovi

- `RSX_DUMP_FRAME=<n>` — salva in `/tmp/rsx_frame.ppm` il primo fotogramma con
  piu' di n disegni, e in `/tmp/rsx_surf_<offset>.ppm` le superfici fuori
  schermo. E' il modo giusto di guardare un fotogramma pieno.
- `RSX_TEX_SAVE=1` — le prime texture decodificate su file.
- `RSX_TEX_ARRAY=1` — shader incorporato con dichiarazione ad array (la prova
  A/B del punto 2).
- `RSX_RTT=0` — disattiva il render-to-texture.
- `RSX_SCISSOR=0` — disattiva il ritaglio.
- `RSX_ONLY_SMALL=1` — salta i quad a schermo pieno.
- `RSX_FRAMEDBG=1` ora stampa anche superfici, dimensioni, ritagli e disegni
  degeneri.

### Il prossimo passo

1. Le proporzioni dell'oggetto disegnato (striscia verticale invece che
   immagine piena).
2. Il titolo raggiunge lo stato che disegna contenuto **a intermittenza**: su 6
   tentativi consecutivi puo' non arrivarci mai. E' lo stallo del modo A della
   sessione 20, che sotto contesa torna. Finche' c'e', ogni misura grafica
   richiede piu' tentativi.
3. Difetto noto del decompilatore dei fragment program: emette `return r[0]`
   anche quando l'ultima scrittura e' al banco a mezza precisione `h[0]`.


## Sessione 21 — perche' lo schermo era nero: il titolo disegna in modalita' immediata

La sessione 20 chiudeva con "la finestra e' nera, e' il buco del backend Metal
(vertex program, depth, RTT)". **Era una diagnosi sbagliata**, o meglio: non era
quello a fermare l'immagine. Il motivo vero e' molto piu' a monte.

### La misura che cambia tutto: zero draw al backend

Nuovo strumento `RSX_FRAMEDBG=1`: una riga per fotogramma con quanti draw e
quanti vertici arrivano al backend Metal, e dove finiscono in clip space.

```
[RSX frame#300] draws=0 verts=0 dentro-clip=0
```

**Zero draw, sempre.** Quindi non e' il vertex program, non e' la matrice, non
e' il depth: al renderer non arrivava proprio niente da disegnare.

### Dove si perdevano: il titolo non usa DRAW_ARRAYS

`RSX_LIVE_FEED_DBG=1` conta i metodi di disegno nella FIFO:

```
[feed] begin_end=6674 draw_arrays=0 draw_index=0
```

Migliaia di coppie BEGIN/END, e **nemmeno un DRAW_ARRAYS o DRAW_INDEX_ARRAY**.
Il nuovo `RSX_METHIST=1` (istogramma dei metodi fra un BEGIN e il suo END) dice
con che cosa disegna davvero, su 400 primitive:

```
0x1880 1436   0x1884 1436   0x1888 1436   0x188C 1436      (SET_VERTEX_DATA2F_M)
0x1C20 1436   0x1C24 1436   0x1C28 1436   0x1C2C 1436      (SET_VERTEX_DATA4F_M)
```

E `RSX_METSEQ=1` da' l'ordine crudo dentro una primitiva:

```
0x1808 = 8            <- BEGIN, primitiva 8 = QUADS
0x1C20..0x1C2C = 0    <- attributo 2, 4 float
0x1888, 0x188C = 0    <- attributo 1, 2 float
0x1880 = -1.0  0x1884 = 1.0   <- attributo 0: la POSIZIONE, scritta per ultima
```

Cioe': **modalita' immediata**. Il titolo non riempie un vertex buffer, scrive
ogni vertice come una raffica di registri di attributo fra BEGIN e END, e il
vertice e' completo quando arriva l'attributo 0. Esattamente come glVertex su
OpenGL immediato. Nel port quel percorso **non esisteva**: `rsx_commands.c`
trattava `0x1C00+` come costante di attributo (giusto quando l'array e'
disabilitato, sbagliato dentro BEGIN/END) e `0x1880` non lo gestiva affatto.

### Cosa e' stato aggiunto

1. `rsx_commands.c`: `SET_VERTEX_DATA2F_M` (0x1880, due float, z=0 w=1 come
   sull'hardware) accanto al gia' presente `4F`; e, dentro BEGIN/END, la
   scrittura dell'**attributo 0** chiude un vertice e lo accumula. A END i
   vertici accumulati vanno al backend in un colpo solo.
2. `rsx_commands.h`: nuova voce **opzionale** della vtable,
   `draw_inline(userdata, primitive, verts, count)` — `verts` e' contiguo,
   `RSX_MAX_VERTEX_ATTRIBS x 4` float per vertice. Un backend che la lascia
   NULL si comporta come prima: **D3D12 e null non cambiano di una riga**.
3. `rsx_metal_backend.m`: `mtl_draw_inline`, con la stessa espansione delle
   primitive di `emit_vertices` (Metal non ha quad ne' fan).

Lo store dei vertici e' un file-static in `rsx_commands.c`, non un campo di
`rsx_state`: quella struttura viene copiata e passata in giro, e mezzo megabyte
di vertici dentro non ci deve stare.

Mappatura degli attributi, **letta dai dati**, non indovinata:

```
v0 attr0=(-1.000 1.000 0.000 1.000)  attr1=(0.000 0.000 ...)  attr2=(0.000 ...)
v1 attr0=( 0.609 -0.444 ...)         attr1=(0.125 0.000 ...)  attr2=(0.132 0.132 0.132 0.000)
```

attr0 = posizione (gia' in coordinate normalizzate -1..1: il vertex program per
questi passaggi e' un pass-through, quindi la matrice resta l'identita' —
`RSX_INLINE_MVP=1` applica comunque le costanti 0..3 per il confronto);
attr1 = coordinate di texture (0..1, variano lungo il quad); attr2 = colore.
`RSX_INLINE_MAP=col,tc` rimappa senza ricompilare.

### Il risultato, misurato

```
[RSX frame#300] draws=3 verts=18 dentro-clip=18  x[-1.00 1.00] y[-1.00 1.00] z[0.00 0.00] w[1.00 1.00]
```

Da 0 a 18 vertici per fotogramma, **tutti dentro il volume di clip**. E con
`RSX_FORCE_BUILTIN=1 RSX_DEBUG_TINT=1` (shader incorporato, tinta fissa) la
finestra si riempie: **3.680.414 pixel** del colore di prova. La catena
FIFO -> vertici immediati -> Metal -> schermo funziona end-to-end.

### Perche' lo schermo e' ANCORA quasi nero (e non e' piu' un difetto)

Con gli shader veri il fotogramma resta nero, e ora si sa perche':

- Il titolo disegna **2-3 quad per fotogramma**, non di piu'.
- Il primo e' un quad a schermo pieno di colore `(0,0,0,0)`: di fatto un clear a
  nero. Lo fa lui, e' giusto che sia nero.
- Gli altri sono piccoli quad con texture (unita' 0, 4096x128, formato `0xA6` =
  DXT1 | LN). La texture **e' decodificata bene** — `RSX_TEX_DBG=1`: 65.464
  texel non neri su 524.288, 48.170 con alpha — ma quei quad campionano la
  fascia `u in [0, 0.031]` dell'atlante, che li' e' vuota.
- Verificato saltando il quad a schermo pieno (`RSX_ONLY_SMALL=1`): dentro
  l'area di contenuto restano **zero** pixel disegnati (i 2.416 trovati stanno
  tutti sulla riga y=1555, cioe' il bordo della finestra).

Il fragment program del guest si traduce e compila (`RSX_FP_DUMP=1`): due
istruzioni, `r0 = tex2D(tc0)`, `h0 = r0 * col0`. Nota per dopo: **emette
`return r[0]` mentre l'ultima scrittura e' a `h[0]`** — sul ramo a mezza
precisione l'uscita dovrebbe venire da `h`. Qui non cambia nulla (entrambi sono
neri), ma e' un difetto vero del decompilatore, da correggere quando servira'.

### Il prossimo passo, e non e' piu' il renderer

**La geometria della scena non viene mai sottomessa.** Zero `DRAW_ARRAYS` e zero
`DRAW_INDEX_ARRAY` in tutta la corsa: il titolo non sta disegnando un mondo, sta
disegnando una schermata di caricamento fatta di due quad. Il renderer non e'
piu' il collo di bottiglia — lo e' il fatto che il gioco non arriva a produrre
i comandi di disegno della scena, che e' il lato SPU/job (le liste di comandi
del rendering le costruisce il job manager). Il fronte torna li'.

### Strumenti nuovi

- `RSX_FRAMEDBG=1` — per fotogramma: draw, vertici, quanti dentro il clip, e i
  bounding box di x/y/z/w dopo la matrice. E' la prima cosa da guardare davanti
  a uno schermo nero: separa "nessun draw" da "draw fuori schermo" da "draw
  dentro ma invisibili".
- `RSX_METHIST=1` — istogramma dei metodi fra BEGIN e END (dice COME disegna).
- `RSX_METSEQ=1` — la sequenza cruda con i valori (dice in che ORDINE).
- `RSX_INLINE_DBG=1` — i draw in modalita' immediata e i primi vertici.
- `RSX_INLINE_MAP=col,tc` / `RSX_INLINE_MVP=1` — rimappatura attributi / matrice.
- `RSX_FORCE_BUILTIN=1`, `RSX_DEBUG_TINT=1` — shader incorporato, tinta fissa:
  separano "non rasterizza" da "rasterizza nero".
- `RSX_TEX_DBG=1` — quanti texel non neri sono davvero atterrati.
- `RSX_FP_DUMP=1` — l'MSL del fragment program tradotto.
- `RSX_ONLY_SMALL=1` — salta i quad a schermo pieno.


## Sessione 20 — lo stallo dopo il caricamento era il job manager, non l'event flag

Due correzioni alla diagnosi della sessione 19, entrambe misurate, e la causa
vera con la sua soluzione.

### La SPU pone l'event flag, eccome — la finestra DMA era cieca agli atomici

La sessione 19 concludeva "zero scritture della SPU in `0x2A870C80`" con
`SPU_DMA_EAWIN`. **Quella finestra non vede GETLLAR/PUTLLC**: gli atomici
passano da `spu_mfc_atomic()` in `spu_channels.c` e ritornano PRIMA di
`mfc_do_transfer()`, dove sta la stampa. Lo strumento giusto e' `SPU_ATOM_EA`
(ora con `SPU_ATOM_MAX` per alzare il tetto di 48 righe):

- `SPU_ATOM_EA=0x2A870C80 SPU_ATOM_MAX=100000`: **462 PUTLLC** dell'immagine 1
  (`pc=0x0958C lr=0x034AC`), uno per ogni push nella coda lock-free, ognuno
  con `STORE=00010000 ...` cioe' `events |= 1`. La struttura dice
  `direction=1` (SPU→PPU), `clearMode=0` (auto).
- `cellSpursEventFlagWait` esce ogni volta: **zero righe `BLOCKED`** in
  150 secondi (la stampa scatta dopo 1 s di attesa).

Quindi il PPU non aspettava un event flag. Aspettava davvero la parola a
`0x20000000`, come diceva la prima parte della sessione 19.

### La stretta di mano su `0x20000000`, misurata (SPU-FLAG-02)

`YDKJ_WWATCH=0x20000000` + `SPU_DMA_EAWIN=0x20000000:0x20000010`, per fotogramma:

```
PPU  write32 0x20000000 = 0      (guest-fn 0x00566980, poi 0x00566350)  <- arma
PPU  sys_event_port_send(port=1,2, data=0x0FEFCA84) -> "Job Manager Event Handler"
SPU  img=2 pc=0x06120 PUT ea=0x20000000 dati=00000001 00000000            <- fine
SPU  img=1: 5-7 job dalla coda lock-free, ognuno con un PUTLLC sull'event flag
```

Allo stallo: **armato e mai finito** (`chiesti == finiti + 1`). L'ultimo
`write32 = 0` c'e', il PUT della SPU no, e i job della coda lock-free dopo di
esso sono 12 invece di 6 (il thread che li accoda va avanti un po' da solo).

### Chi esegue il lavoro del fotogramma: `wid=4`, job list `0x2A8D1C00`

Nuovo strumento `SPURS_DISPATCH_TRACE=1` (`cellSpurs.c`): una riga per ogni
corsa del policy module che era stata chiesta (kick) o che ha trovato lavoro
(`polls>0`), con wid, argomento (EA della job list), polls, exit. Nel ciclo
normale:

```
[spurs-disp] wid=4 kicked=1 arg=0x2A8D1C00 polls=1 exited=1 rdy_now=7   <- il job
[spurs-disp] wid=6 kicked=1 arg=0x106F0780 polls=0 exited=1
[spurs-disp] wid=4 kicked=1 arg=0x2A8D1C00 polls=0 exited=1 rdy_now=0
```

e il PPU, dopo ogni job, fa **`ResetList`** sulla job list (`0x0073FDF0`:
azzera `+0x20..+0x7F`, scrive `FFFF` nelle sei corsie `+0x14..+0x1E`, zero in
`+0x10/+0x12`), poi accoda il job successivo e chiama
`cellSpursReadyCountStore(wid=4, 8)`.

Allo stallo:

```
[spurs-disp] wid=4 kicked=1 arg=0x2A8D1C00 polls=0 exited=0 rdy_now=7
[spurs-disp] wid=4 kicked=1 ...                 polls=0 exited=0 rdy_now=6
   ... otto volte, fino a rdy_now=0, poi piu' niente
```

Otto corse consecutive del job manager su `wid=4`, ognuna termina subito con
`stop` a `pc=0x02CE0` (l'uscita "niente da fare", `lr=0x9C0`), il readyCount
si consuma fino a zero e nessuno lo rialza. Il job c'e' (il PPU l'ha accodato
e armato), **il manager non lo vede**. I due thread del kernel SPURS non sono
bloccati (lldb: entrambi in `nanosleep`, il ciclo di 1 ms gira).

### LA CAUSA: il contesto persistente del policy module

`spurs_policy.c` tiene la local store del policy module **da una corsa
all'altra** (`s_pm_ctx[wid][spu]`), "perche' un PM vero resta residente sulla
SPU". Ma un kernel SPURS vero ricarica il PM da zero **ogni volta che cambia
workload** sulla SPU — cioe' continuamente, con 8 workload su 5 SPU — quindi
WwsJob non puo' fidarsi di niente che abbia lasciato in LS, e infatti rientra
correttamente da un contesto pulito. Con la LS conservata, dopo il `ResetList`
del PPU il manager riparte da uno stato che dice "gia' fatto" e non rilegge la
lista.

Il runtime aveva gia' l'interruttore A/B: `SPURS_PM_FRESH=1` (contesto
azzerato e immagine ricaricata a ogni dispatch). Misura, corse da 150 s:

| | contesto persistente (default runtime) | `SPURS_PM_FRESH=1` |
|---|---|---|
| fotogrammi completati (PUT a 0x20000000) | 57, 62, 16, 32 → **stallo** | **539, 366** → nessuno stallo |
| spin del PPU su 0x20000000 | si', 3 corse su 3 | **mai**, 2 su 2 |
| caricamento (SendSignal / Pread / RequestLevel) | 462 / 79 / 4 | 462 / 79 / 4 (identico) |

`SPURS_PM_FRESH=1` e' ora il default in `run.sh` (parametro del titolo, fuori
dal repo, come gli altri). Il commento in `spurs_policy.c` dice gia' che la
persistenza era una pezza per un bug dell'r4 poi risolto.

**Non e' una cura al 100%, e' una forte mitigazione.** Corsa pulita (macchina
scarica): 524+ fotogrammi e ancora vivo, un solo `spinning` transitorio a
confine di fotogramma. Ma sotto contesa (8 corse consecutive back-to-back +
questa sessione) il modo A ritorna in modo erratico: PUT per corsa
921/191/757/113/304/**4/28**/184 — due corse si sono fermate a 4 e 28
fotogrammi. E' la trappola gia' annotata: **misurare con la macchina occupata
porta fuori strada.** La causa residua del modo A resta da trovare (allo
stallo la coda lock-free e' comunque vuota, `pop1==push1`).

La finestra e' **nera**: `screencapture -l <id>` su una corsa da 524 fotogrammi,
centro `(0,0,0)`, l'unico bianco e' la barra del titolo. Il gioco disegna (la
FIFO drena comandi di draw) ma niente arriva a schermo: e' il buco del backend
Metal gia' elencato (vertex program in MSL, depth/stencil, MRT, RTT, crossbar
delle texture). E' il prossimo fronte, non lo stallo.

### Il secondo modo di fermarsi: "Rendering is stuck on reference value N"

Resta il blocco RSX (1 corsa su 2-5, anche con FRESH). Firma nel log, sempre
una sola riga:

```
[putdrain] NON arrivato a put: get=0066xxxx put=0001x008 ref=N
Rendering is stuck on reference value N+1. Starting infinite heartbeat loop.
```

**RISOLTO (o quasi).** Con il trace JUMP/CALL/RET (`GCM_RECDBG=1`) il walker
gira in un ciclo di due archi che non arriva mai a `put`:

```
[JMP] 00660AB0 -> 00010000 (put=00011008)
[JMP] 00010008 -> 00660000 (put=00011008)   ... 500.000 volte, budget esaurito
```

Il rilevatore di cicli c'e' gia' (hash del target, `(tgt>>2)&255`) ma **non lo
prende**: `0x10000` e `0x660000` collidono sullo stesso bucket (entrambi hash
0), quindi `s_seen_tgt[0]` oscilla fra i due e non segnala mai. Il walker
cammina fino al tetto di 0x100000 parole ed esce muto.

Il fix e' lo stesso di `GCM_STRAND_FIX`: l'anello e'
`[SET_REFERENCE][valore][JUMP]`, `put` sta subito dopo il valore, e se a
`put`-8 c'e' un'intestazione SET_REFERENCE ci si rimette in pari da li'. Scatta
**solo a budget esaurito** — uno stato in cui la FIFO e' gia' morta — quindi
non puo' peggiorare un caso sano. Misura: 8 corse, **zero** "stuck on
reference" (prima 1 su 2-5), il recupero scatta ogni volta che serve
(`recover==budget`). La stampa `NON arrivato` ora riporta `[get-4]`/`[get]` e il
budget esaurito nomina il ciclo.

### Strumenti nuovi

- `SPU_ATOM_MAX=<n>`: tetto delle righe `[atom-ea]` (default 48).
- `SPURS_DISPATCH_TRACE=1`: `[spurs-disp]` per ogni corsa del PM chiesta o
  produttiva; e `[spurs-pm] END ...` per ogni corsa che non passa da
  exitToKernel (attenzione: e' anche l'uscita normale "niente da fare" a
  `pc=0x02CE0`, quindi inonda — usarla solo con `grep -v 'pc=0x02CE0'`).
- `[putdrain] NON arrivato` stampa anche le parole a `get-4` e `get`.
- `GCM_RECDBG=1`: `[JMP]`/`[CALL]`/`[RET]` del walker della FIFO (chi salta
  dove), piu' `[SEMA]`, `[REC>]`. Serve a vedere i cicli di JUMP.
- `[cellGcmSys] budget esaurito ...`: il walker ha girato 0x100000 parole
  senza arrivare a `put` (ciclo di JUMP); dice se e' scattato il recupero.
- Screenshot della finestra: `swift scratchpad/winid.swift <pid>` da' l'id,
  poi `screencapture -x -l <id> out.png`.


## Sessione 19 — il segmento tagliato sotto i piedi

Il blocco `Rendering is stuck on reference value N` è risolto: **4 corse su 5
non si bloccano più**, e i fence drenati per corsa passano da ~75 a ~2900.

### Come è fatta la FIFO di Uncharted

Non è l'anello di libgcm. Il titolo tiene:

- un **anello di 4 slot da 4 KB** a io `0x10000`–`0x14000`. Ogni slot è
  `[SET_REFERENCE][valore del fence][JUMP -> segmento]`, e `put` viene sempre
  posato **subito dopo il valore**, cioè a `slot+8`.
- due **segmenti di comandi da 2 MB** a io `0x660000` e `0x860000`, usati a
  turno. Il segmento raggiunto dallo slot *k* finisce con un JUMP verso lo slot
  *k+1*.

Quindi ogni movimento di `put` fa consumare alla RSX un segmento e un fence. Il
contesto gcm del titolo sta a EA `0x1068B1A4` (begin/end/current/callback a
+0/+4/+8/+0xC) e il callback a +0xC **non è un recycle**: è una `printf` di
errore. Il recycle non esiste; il titolo non ne ha bisogno.

### Il difetto

Con una traccia unica che mette in ordine `put`, i fence negli slot, i JUMP di
coda e gli intervalli di scrittura dell'emettitore (`GCM_SEQ=1`), la sequenza
di un frame normale è:

```
LEGGE ctrl+8 (=75)
EMIT   [21660000 .. 216608D4]  (2264 byte)
SLOT   [21012008] = 20660000        slot2 -> segmento A
JUMP   [216608D8] = 20013000        coda del segmento -> slot3
SLOT   [21013004] = fence
PUT    = 00013008
```

e quella del frame che blocca è:

```
EMIT   [21660000 .. 21660054]  (88 byte)
JUMP   [21660058] = 20013000        coda a 0x58
SLOT   [21013004] = fence
EMIT   [21660058 .. 216604C4]  (1136 byte)   <- SOPRA il JUMP di coda
PUT    = 00013008
```

Il titolo chiude il segmento con un JUMP **provvisorio**, scrive il fence, e
poi ci accoda sopra altri comandi partendo esattamente da quel JUMP — che
sparisce — e solo dopo muove `put`. Quando la passata entra nel segmento (cosa
che può fare solo quando `put` si è mosso, come la RSX vera) l'uscita non c'è
più: si cammina fino alla testa di scrittura e si resta lì. Il titolo aspetta
il fence che ha appena accodato e non lo vede mai.

### La correzione

Tre pezzi, tutti attivi per default in `run.sh`:

1. **`GCM_DRAIN_ON_PUT`** — il drain avviene sulla scrittura di `put`, che è
   quando la RSX vera comincia a consumare. Misurato: `get` raggiunge `put` a
   ogni frame e `ref` avanza 1:1.
2. **`GCM_DRAIN_ONLY_PUT`** — niente drain a orologio. Un drain a 4 ms può
   entrare in un segmento che il titolo sta ancora riempiendo.
3. **`GCM_STRAND_FIX`** — quando la passata si arena su un segmento senza
   uscita, il fence atteso sta **sempre due parole prima di `put`**: se lì c'è
   davvero un'intestazione `SET_REFERENCE`, il JUMP mancante puntava a quello e
   si riprende da lì. Stessa regola nella risincronizzazione, che ora
   **continua** la passata invece di uscirne — uscendo, quel fence si sarebbe
   letto solo al prossimo movimento di `put`, che non arriva mai perché il
   titolo sta aspettando proprio quello.

Si perdono i comandi del segmento oltre il punto in cui ci si è arenati. Sono
già irraggiungibili, ed è esattamente ciò che la RSX vera avrebbe consumato
prima che il titolo li sovrascrivesse.

### Strumenti nuovi

- **`YDKJ_VWATCH=<lo>:<hi>`** — guarda il **valore**, non l'indirizzo. Il
  contesto gcm del titolo non si trova per indirizzo: si trova perché qualcuno
  gli scrive dentro un puntatore al proprio command buffer.
- **`YDKJ_WWATCH_LEN`** — la finestra della watch non è più fissa a 0x40.
- **`GCM_SEQ=1`** — `put`, fence, JUMP di coda, intervalli dell'emettitore e
  letture di `ctrl+8`, in un unico flusso, con il thread che scrive.

### Dove arriva ora il titolo

In una corsa da 300 secondi senza blocchi il gioco supera la fase in cui era
fermo da sempre e comincia a caricare **contenuto vero**: pacchetti di livello
(`PackageMgr::RequestLevel [particle-ingame]`, `[main/pak19/post]`), script
particellari, le cinematiche in-game (`igc-manager.cpp`, 642 registrazioni
IGC), le voci (829 `Registering VOX`), i sottotitoli e la telecamera di
`movie1`, e le icone dell'HUD (`Loading HUD icon 'assault-rifle-a'`,
`'bullets'`). Niente di tutto questo era mai comparso.

### Il fronte nuovo: dopo il caricamento

Il titolo non si blocca più sulla FIFO, ma dopo la fase di caricamento si ferma
su un'attesa. Misurato (corsa da 200 s, senza contesa sulla macchina):

- La FIFO **continua a drenare fino all'ultima riga di log**: 6.050 fence in
  200 secondi, cioè ~30 al secondo. Il gioco disegna, non è morto.
- Il thread principale del PPU però gira per 149.240 righe consecutive sullo
  stesso punto: `func_00566108`, chiamata da `func_005324E8` con
  `lr=0x00532620`. Il ciclo aspetta il **bit 0 della parola a `0x20000000`**,
  presa da `[oggetto+0x320]`. La stessa funzione, subito dopo, aspetta una
  seconda lista di completamenti da `[oggetto+0x314]` e poi libera un buffer:
  è una sincronizzazione di fine fotogramma.
- La SPU **sa** scrivere quel flag: `img=2 pc=0x06120 PUT lsa=0x10400
  ea=0x20000000 size=0x10`, con dati `00000001 00000000`. Lo fa alcune decine
  di volte e poi smette.
- Il task 0 del taskset `0x2A86F380` (immagine SPU **1**, non 2) si parcheggia
  sempre nello stesso punto: `pc=0x00A70`, l'intercettazione della syscall del
  taskset, con oggetto d'attesa nullo.
- Le `SendSignal` che lo risvegliano **non le chiama il gioco**: le chiama la
  nostra HLE di `_cellSpursLFQueuePushBody` (NID `0x8A85674D`), cioè il PPU
  accoda un lavoro in una coda lock-free SPURS da `func_0072EB28` e la push
  sveglia il consumatore. 462 volte per corsa, poi basta.

Quindi ci sono **due meccanismi distinti** e non vanno confusi: la coda
lock-free servita dall'immagine 1, e il lavoro dell'immagine 2 che scrive il
flag a `0x20000000`. Il PPU aspetta il secondo.

Da notare, come avvertimento: sotto contesa (con il worker locale che occupa la
GPU) il titolo si ferma **in un punto diverso** — `HOTREAD8 spinning on
0x0FEFCB30 val=0x05 guest-fn=0x001AD590`. Il punto d'arresto non è unico, e
misurarlo con la macchina occupata porta fuori strada.

#### La catena di un fotogramma, e dove si spezza

Misurato su una corsa da 150 s (`/tmp/stallo.log`, 127.133 righe). Il ciclo
normale, sempre uguale, è:

```
[cellKb] SetReadMode / SetCodeType            inizio fotogramma
[WWATCH] 0x20000000 = 0   (func_00566980)     arma il flag
[WWATCH] 0x20000000 = 0   (func_00566350)     arma il flag (secondo)
[evt] port_send(port=1) -> queue id=1
[evt] q=1 receive RETURNED to tid=2           il Job Manager riceve
[evt] port_send(port=2) -> queue id=2
[spurs-pm] poll #1 (r3=0x1) -> continue       gira il policy module
[dma-eawin] img=2 pc=0x06120 PUT ea=0x20000000  dati: 00000001   la SPU finisce
```

L'ultimo giro è identico **fino a `port_send(port=2)` compreso**, e poi non
succede più niente: il policy module non gira (ultimo `poll` alla riga 12979,
ultimo `port_send` alla 13004) e il completamento non arriva mai.

I numeri:

- lavori armati 114, completati 57 — esattamente due armamenti per lavoro;
  l'ultimo armamento (riga 12999) è **dopo** l'ultimo completamento (12980).
- **Dopo la riga 13000 la SPU non fa assolutamente più nulla**: zero occorrenze
  di `spu-park`, `WAIT_SIGNAL#`, `SendSignal`, `wws-cmd`, `spu-ovl`,
  `spu-assert`, `mfc-list`, `dma-eawin`, `cellSpurs`.
- Il thread `tid=2` (Job Manager Event Handler) finisce fermo in
  `event_queue_receive(q=1)` da `lr=0x0073E800`, con la coda **vuota**
  (`qcount now 0` in tutte e 56 le ricezioni).
- La coda 2 non la riceve **nessuno**: 56 invii, 0 ricezioni. È così da sempre,
  anche nei giri che funzionano, quindi non è lei il motore.

#### Non è un risveglio perso

Alzando i tetti delle stampe (`spu-park` era tappato a 400, `signal task` a 24)
si vede che segnali e parcheggi si **alternano uno a uno**: 321 segnali, 322
parcheggi. Il task viene svegliato, lavora, si riparcheggia; l'ultima volta
nessuno lo sveglia più. Le sveglie arrivano dalla nostra HLE di
`_cellSpursLFQueuePushBody`, cioè dal PPU — che a quel punto sta già aspettando.

È uno stallo simmetrico: **il PPU non accoda più finché il lavoro non finisce,
la SPU non fa niente finché non le si accoda qualcosa.**

#### Ipotesi respinta: `LBP_WS_DRAIN`

Nel runtime c'è un interruttore diagnostico che, dopo N secondi senza segnale,
fa tornare il task come se il kernel l'avesse ripreso "senza lavoro pendente".
Provato con `LBP_WS_DRAIN=2`: **peggiora**. Il task riprende con lo stato non
pronto e va fuori strada — `[mfc] REJECTED malformed transfer: ea=0x111DFE01`
ripetuto, poi `[spu-depth] lifted call recursion, halting the SPU` a profondità
2000. La corsa si ferma prima di quella senza (8 lavori armati contro 114).
Fabbricare la ripresa non è la strada.

#### Il PPU il lavoro lo accoda eccome

Un anello dei 96 import piu' recenti (`ppu_hle_dump_ring`, stampato una sola
volta quando lo spin viene riconosciuto) dice esattamente cosa fa il PPU prima
di fermarsi:

```
cellSpursEventFlagClear     lr=0x0009ED34
_cellSpursLFQueuePushBody   lr=0x0072EC00
cellSpursEventFlagWait      lr=0x0009F2A0
```

Azzera un event flag SPURS, **accoda il lavoro**, e aspetta quell'event flag.
Quindi l'ipotesi precedente ("arma e aspetta senza accodare") e' sbagliata.

E la push funziona davvero: con `PS3_LFQ_TRACE=1` si vedono 512 push per corsa,
i dati copiati negli slot (`push pos=7 slot=0x2A870E60 size=32`) e la sveglia
mandata al taskset (`sveglio arg=0 su eaSignal=0x2A86F380`).

#### Il consumatore prende il lavoro e non lo finisce

L'ordine alla fine e' inequivocabile:

```
SendSignal(taskset=0x2A86F380 id=0) lr=0x0072EC00     la push sveglia
[spu_workload] signal task 0                          il segnale arriva
[spu-park] img=1 pc=0x00A70 task=0                    il task si RIPARCHEGGIA
```

...e la fotografia della coda nell'istante dello spin dice che **la coda e'
vuota**:

```
[cellSync] coda 0x2A870D00  pop1=8 push1=8  depth=12 size=32
```

`pop1 == push1`: tutto quello che e' stato spinto e' stato anche tolto. La SPU
ha preso il lavoro e non l'ha portato a termine -- il completamento a
`0x20000000` non arriva e l'event flag non viene mai posto.

#### Il DMA di completamento: chi lo chiama e sotto quale condizione

A `0x6120` non ci arriva nessun salto statico (`grep` nel job module trova solo
la definizione e la tabella): ci si arriva con un salto **indiretto**. Il
registro di collegamento della SPU, stampato al momento del trasferimento, dice
il chiamante:

```
[dma-eawin] img=2 pc=0x06120 PUT lsa=0x10400 ea=0x20000000 size=0x10
            ritorno a 0x048D8  (r7=0x00010400 r8=0x0000001F r9=0x20000000)
```

`0x48D8` e' il ritorno dalla chiamata a `spu_func_00005F88` fatta da
`spu_func_000048D0`, e ci si arriva solo per questo bivio:

```c
void ..._spu_func_000048C8(spu_context* ctx) {
        ctx->gpr[4] = spu_ls_read128(ctx, ctx->gpr[1]._u32[0] + 0xC0);
        if (ctx->gpr[4]._u32[0] != 0) { ... 0x4CD8 ... }   // NON completa
        { ... 0x48D0 ... }                                 // completa
}
```

cioe' il completamento avviene solo se la parola a `stack+0xC0` e' zero.

#### Il punto esatto dello stallo

Il task SPU **non si sveglia e riparcheggia a vuoto**: il campo `ran=` degli
ingressi in `WAIT_SIGNAL` dice che ogni ciclo gira davvero, fra 3 e 8 ms, e
l'ultimo (`WAIT_SIGNAL#200`) gira 4 ms come tutti gli altri. E su 200 cicli
solo 57 producono un completamento: **la maggior parte dei cicli non completa
niente, ed e' normale.**

Quindi la sequenza finale e': il PPU accoda il lavoro N e aspetta; la SPU viene
svegliata, gira 4 ms, non completa N, si riparcheggia; e nessuno la sveglia piu'
perche' l'unica cosa che la sveglia e' una nuova push del PPU — che sta
aspettando N.

**L'ipotesi architetturale** (da verificare, non ancora dimostrata): sul ferro
vero il kernel SPURS rimette in esecuzione il task finche' il workload ha
lavoro, mentre da noi il task dorme finche' non arriva un segnale esplicito. Se
e' cosi', il lavoro residuo di N non e' nella coda (che allo stallo e' vuota,
`pop1 == push1 == 8`) ma nello stato interno del job, e serve un criterio per
rimettere in esecuzione il task senza fabbricarne la ripresa — cosa che
`LBP_WS_DRAIN` fa male, come misurato sopra.

#### LA CAUSA: un event flag che nessuno pone

Il PPU non aspetta la parola a `0x20000000`: aspetta un **event flag SPURS**.
L'ultimo import prima dello spin e' `cellSpursEventFlagWait`, e la nostra
implementazione stampa su chi aspetta:

```
[cellSpurs] EventFlagWait#1 flagEA=0x2A870C80 pattern=0x0001 events=0x0000
```

Aspetta il bit 0 del flag a `0x2A870C80` — che sta 0x80 byte prima della coda
lock-free (`0x2A870D00`), cioe' nello stesso blocco di strutture.

Due misure, entrambe a zero:

- **`cellSpursEventFlagSet` non viene chiamata mai.** La funzione stampa a ogni
  chiamata (prime 40, poi una ogni 1000) e nel log non compare nemmeno una volta.
- **La SPU non scrive mai in quella struttura.** Con
  `SPU_DMA_EAWIN=0x2A870C80:0x2A870D00`: zero trasferimenti.

E non e' un problema di risveglio della nostra HLE: `cellSpursEventFlagWait` e'
a polling e **rilegge la struttura guest** a ogni giro, quindi anche una
scrittura diretta della SPU la vedrebbe. Semplicemente non c'e'.

Quindi: la SPU fa il lavoro (57 completamenti scritti a `0x20000000` per corsa,
cicli da 3-8 ms), ma **non arriva mai al codice che pone l'event flag** che
sblocca il PPU. E' esattamente il ciclo aperto gia' annotato nel runtime
(`SPURS_EF_SPU_REPLY` in `spu_channels.c`): "il task guest dovrebbe porre il
proprio flag SPU->PPU a fine ciclo, e non lo fa mai".

#### RETTIFICA: la SPU legge tutto, il tetto del log ingannava

Una prima lettura diceva che la SPU smetteva di leggere la coda duemila righe
prima dello stallo. **Era falso**: la finestra `SPU_DMA_EAWIN` stampava al
massimo 200 righe (`if (_n++ < 200)`), e quelle 200 finivano a meta' corsa. Il
tetto e' stato alzato a 100.000 e la misura rifatta:

- **512 trasferimenti letti dalla coda, esattamente quanti i 512 push.**
- L'ultimo pop e' alla riga 13616, lo spin del PPU comincia alla 14281: 665
  righe dopo, non duemila prima.

Quindi **la SPU consuma tutto quello che le viene accodato, fino all'ultimo
elemento**. Il consumatore funziona. Lezione: prima di concludere "X smette di
fare Y" da un log, controllare che la stampa di X non sia tappata.

E i puntatori dell'header lo confermano: a ogni push `[ea+0x00]` (pop) e
`[ea+0x08]` (push) sono uguali, cioe' la SPU ha gia' consumato. Ne' Clear ne'
una pop lato PPU vengono mai chiamate (misurato: zero e zero), quindi a muovere
il puntatore di pop e' la SPU stessa, come dev'essere.

#### Il prossimo passo

Resta quello di prima, e ora e' l'unico: la SPU processa l'ultimo elemento e
**non pone l'event flag a `0x2A870C80`** che sblocca il PPU. Zero chiamate a
`cellSpursEventFlagSet`, zero scritture della SPU in quella struttura. Serve
trovare, nel task SPU, dove dovrebbe porlo.

Attenzione, due strumenti che **non** servono per questo, entrambi verificati:
`SPU_PCWATCH` vede solo i blocchi raggiunti per trasferimento (le funzioni
chiamate direttamente non ci passano), e `LBP_WS_DRAIN` fabbrica la ripresa e
manda il task fuori strada.

## Sessione 18d — e ora non aspetta piu' nemmeno la RSX

Il gioco si fermava su `Rendering is stuck on reference value 76`. Tre difetti
in fila nel walker della FIFO, tutti trovati misurando.

### 1. Una sequenza di zeri non e' un flusso di comandi

Una parola a zero veniva trattata come metodo 0 con conteggio 0, cioe' un NOP
da 4 byte. In un buffer non ancora riempito il walker avanzava cosi' nel vuoto,
e siccome quel vuoto sta **fuori dal ring** il controllo "fermati a `put`" non
scattava mai: usciva dal segmento, camminava per megabyte, finiva sulla coda di
un altro segmento e da li' tornava nel ring. Il risultato era un ciclo che
ridrenava lo stesso fence **38.863 volte in due minuti**, tenendo `ref`
inchiodato mentre il gioco aspettava il successivo.

Quattro zeri di fila ora fermano la passata **senza far avanzare `get`**: la
passata dopo rilegge le stesse parole, e se restano zero mentre `put` si e'
mosso ci pensa il watchdog di stuck-get. Da 38.863 giri a 1 sola fermata, e il
log di una corsa da 95.185 a 16.472 righe.

### 2. Una risincronizzazione non deve perdere un fence

Con il walker fermo dentro un segmento, il watchdog risincronizza a `put`. Il
tratto saltato pero' puo' aver fatto il **giro dell'anello**: non e'
`[from, put)` ma `[from, fine)` piu' `[inizio, put)`. Lo spazzolamento di
recupero guardava solo la prima meta', e cosi' spariva il fence scritto
all'inizio dell'anello -- la 0x49 a `0x10000` con `put=0x10008`, cioe'
esattamente quella che il gioco aspettava. Perdere un disegno su una
risincronizzazione costa un fotogramma; perdere un fence blocca il titolo.

### 3. Lo spazzolamento non deve diventare esso stesso il problema

Coprire entrambi i tratti, da solo, ha fatto **24 milioni** di ripubblicazioni
in due minuti: il watchdog risincronizza ogni poche passate e rispazzolava
sempre lo stesso tratto. Ora la spazzolata e' una sola per ogni valore di
`put`, e `gcm_ref_push_at` rifiuta due valori uguali di fila.

### Risultato

| | prima | dopo (corsa riuscita) |
|---|---|---|
| fence drenati in due minuti | fino a **76** | fino a **3060-3676** |
| `Rendering is stuck` | sempre | **assente** |

### Ma e' una corsa su quattro

Onesta': con l'albero attuale il blocco si supera **1 volta su 4**. Le altre
tre si fermano a fence 77-81. Cosa NON e' la causa, misurato:

* **non e' la profondita' dello stack SPU.** Sembrava esserlo -- le corse
  bloccate avevano `[spu-depth] host_depth=2000` e quella riuscita no -- ma
  alzando il tetto a 60.000 il gioco resta fermo lo stesso (fence 78), e una
  corsa bloccata ha `depth=0`. Era una coincidenza. L'annidamento e' comunque
  un difetto vero: e' infinito, non profondo, e resta da capire.
* **non e' il semaforo della RSX.** `GCM_SEMA_ACQUIRE=1` (l'ACQUIRE che blocca
  come l'hardware) peggiora: i fence si fermano a 15. Il semaforo blocca e
  niente lo sblocca.
* **non e' il dedupe dei fence.** Una finestra di 64 valori blocca il gioco in
  tre corse su tre: quando il ring rigira, il fence atteso e' gia' nella
  finestra e viene scartato. Solo il valore precedente va scartato.

Quello che si sa: nelle corse bloccate il walker si ferma dentro un segmento
il cui `JUMP` di coda non e' stato scritto, e il gioco aspetta il fence
successivo.

### Cosa e' venuto fuori continuando

* **La sequenza dei fence pubblicati era sporca.** Lo spazzolamento di recupero
  ripescava fence STANTII rimasti nell'anello oltre `put` e li ripubblicava: una
  `SET_REFERENCE 1` del primo fotogramma finiva fra la 73 e la 74, mandando
  `ref` all'indietro sotto gli occhi di chi aspetta per uguaglianza. Ora durante
  un recupero passano solo valori piu' nuovi del piu' alto gia' drenato, e la
  sequenza e' strettamente monotona.
* **L'anello del titolo non e' quello del contesto libgcm.** Il contesto dice
  `begin=0x21000000 end=0x21010000` (io 0..0x10000); i fence stanno a io
  0x10000..0x13000. Il gioco si gestisce una coda propria oltre quella di
  libgcm. Ricavare l'inizio dell'anello dal contesto dava io 0, e la spazzolata
  partiva da li' raccogliendo qualunque cosa somigliasse a un fence in tutta la
  memoria bassa -- il che, per caso, ogni tanto pescava quello giusto e faceva
  passare il gioco. I confini si ricavano ora da dove `put` e' stato visto.
* **Una lezione di metodo, pagata.** `ninja -C build | tail -2` restituisce
  l'esito di `tail`, non di `ninja`: per diverse misure ho creduto di provare
  una correzione mentre eseguivo il binario precedente, e le conclusioni di
  quelle corse erano su codice che non esisteva. Ora la build passa da uno
  script che fallisce rumorosamente.

### Il deadlock, visto per intero

La scansione dell'anello al momento dello stallo lo mostra senza ambiguita':

```
[ringscan] put=00011008 fence nell'anello: 79@11000 75@12000 76@13000
Rendering is stuck on reference value 79
```

Il fence 79 e' scritto a `0x11000`, cioe' a `put - 8`. Il walker pero' non ci
arriva: e' fermo dentro il segmento a `0x660930`, dove il PPU ha smesso di
scrivere a meta' (le sue scritture si vedono una per una con `YDKJ_WWATCH`, e
finiscono di colpo). Il gioco non riprende a scrivere perche' aspetta la RSX.
Cane che si morde la coda.

Provate e scartate, in ordine:

* **spazzolare l'anello in cerca di SET_REFERENCE.** Raccoglie fence stantii e
  spazzatura -- un "fence" 524724 pubblicato fra il 74 e il 78 -- e pubblica
  fuori ordine, con `ref` che oscilla 75, 74, 75, 74. Faceva passare il gioco
  circa una volta su quattro, ma **per caso**: pescando il fence atteso in mezzo
  al resto. Sostituita da un riavvolgimento all'ultima posizione dell'anello,
  che lascia drenare i fence al walker in ordine. Il titolo arriva cosi' a
  fence 71-74 invece di 75-93 e non si sblocca mai: preferisco un blocco onesto
  a una fortuna che nasconde il difetto vero.
* **`GCM_SEMA_ACQUIRE=1`** (l'ACQUIRE del semaforo che blocca come l'hardware):
  peggiora, i fence si fermano a 15.
* **alzare `SPU_HOST_DEPTH_MAX` a 60.000**: nessun effetto, e una corsa bloccata
  ha `depth=0`. La correlazione fra profondita' dello stack e blocco era una
  coincidenza.

### La domanda aperta

**Perche' il PPU smette di scrivere il segmento a meta'?** Le sue scritture si
vedono (`YDKJ_WWATCH=0x21660900`): un ciclo stretto che emette 16 byte per
giro, dagli stessi quattro indirizzi di ritorno host `0x104724f04 / f14 / f20 /
f30`, e poi si ferma. Risolvere quei quattro indirizzi alla funzione guest che
li contiene dice chi emette il buffer e su cosa si mette ad aspettare -- ed e'
li' che va guardato, non nel walker: di euristiche nel walker ne ho aggiunte
quattro e nessuna ha sbloccato il titolo.

Le stringhe vicine nel binario dicono cosa aspetta il gioco: `Wait On Rsx`,
`Wait On Mesh Processing`, `Wait On VSync`, `Out of Command memory`.

## Sessione 18c — il PPU non aspetta piu' la SPU: ora aspetta la RSX

Tre correzioni in fila, e il fronte si e' spostato.

### 1. `SPU_TAILRET` va spento

La sua euristica -- *un trasferimento il cui target coincide con `gpr[0]` E' un
ritorno* -- interrompe il drain, e il policy module usa r0 anche per i salti
**calcolati**: la finestra di interrupt

```
002E1C  ila $r0, 0x2E30
002E20  ila $r2, 0x2E28
002E24  bie $r2          ; abilita gli interrupt, salta a 0x2E28
002E28  ai  $r0, $r0, 0
002E2C  bid $r0          ; disabilita e salta a r0  <- salto calcolato
```

e' insieme il punto in cui lo stall-and-notify entra **e** l'arco di ritorno del
ciclo di polling. L'euristica lo scambiava per un ritorno e abbandonava il ciclo.

Provato anche a prendere l'interrupt **prima** delle scorciatoie (piu' fedele:
sull'hardware un interrupt abilitato entra fra due istruzioni qualunque,
ritorno compreso). Non basta -- 14 comandi in due corse su due -- perche' il
danno non e' solo sull'interrupt. Il riordino resta comunque, perche' e' giusto.

### 2. `SPU_RdMachStat` non e' "la SPU sta girando"

Per il CBEA il bit 0 di quel canale e' lo **stato di abilitazione degli
interrupt**. Restituivamo `status == RUNNING`. Il gioco ci fa affidamento in
entrambi i versi:

* la routine di interrupt del policy module (0x246C) asserisce che sia **0**
  -- passava per caso;
* i moduli di job asseriscono che sia **1** prima di armare una finestra DMA
  (`jobmod_008cc900` a 0x6110 e 0x6024) -- falliva.

E falliva in cascata: dopo il primo `stop` lo stato non torna piu' RUNNING, e
ogni lettura successiva dava 0. Con la correzione (`ctx->int_enable`) le assert
dei moduli di job spariscono, e sparisce anche la crescita dello stack host che
quella cascata provocava (`[spu-depth] host_depth=2000`: da 1 a **0** per corsa).

### 3. Il risultato

| | prima della sessione | ora |
|---|---|---|
| comandi di caricamento eseguiti | 8, tutti indice 1 | **400** (tetto della sonda) |
| moduli di job residenti | 0 | **3** |
| assert scattate | 1-2 per corsa | **0** |
| stack host esaurito | 1 per corsa | **0** |
| interrupt di stall-and-notify | 0-3 | **16** |
| `[HOTREAD] spinning on 0x20000000` | ~90.000 righe per corsa | **0** |

E soprattutto la SPU ora **scrive il flag che il PPU aspettava**:

```
[dma-eawin] img=2 pc=0x06120 PUT lsa=0x10400 ea=0x20000000 size=0x10
```

Il ciclo PPU -> SPURS -> WwsJob -> job -> flag -> PPU e' chiuso.

### Il fronte nuovo: la RSX

Il gioco ora si ferma piu' avanti, e lo dice con parole sue:

```
Rendering is stuck on reference value 76. Starting infinite heartbeat loop.
```

Aspetta che la RSX raggiunga il valore di riferimento 76
(`cellGcmSetReferenceCommand` / NV406E_SET_REFERENCE). Da qui si lavora sul
backend RSX, non piu' sulla SPU.

### SBLOCCATO sul serio: il ciclo Load -> Run -> Store si chiude

La saltuarieta' aveva una causa, ed era la scorciatoia `SPU_TAILRET`. Il suo
controllo dice: *un trasferimento in sospeso il cui target coincide con `gpr[0]`
E' un ritorno* -- e interrompe il drain **prima** del punto in cui l'interrupt
viene preso. Ma il policy module usa r0 anche per i salti CALCOLATI: la finestra
di interrupt di una sola istruzione

```
002E1C  ila $r0, 0x2E30
002E20  ila $r2, 0x2E28
002E24  bie $r2          ; abilita gli interrupt, salta a 0x2E28
002E28  ai  $r0, $r0, 0
002E2C  bid $r0          ; disabilita e salta a r0  <- salto calcolato, non ritorno
```

e' **l'unico punto in cui l'interrupt di stall-and-notify puo' essere preso**, e
l'euristica lo scambiava per un ritorno esattamente li'.

Spegnendolo -- il lavoro che faceva lo copre `SPU_RETLINK`, che confronta con
l'indirizzo di ritorno vero invece di indovinare:

| | `SPU_TAILRET=1` | `SPU_TAILRET=0` + `SPU_RETLINK=1` |
|---|---|---|
| comandi di caricamento eseguiti | 14 | **oltre 400** (tetto della sonda) |
| moduli di job residenti | 1 | **2** (immagini 227 e 212) |
| interrupt di stall-and-notify | 3 | **16** |
| ciclo della pipeline | si ferma dopo il primo job | **si chiude e riparte** |

E lo stato del job manager, che prima restava a 1 per sempre, ora percorre tutto
il giro e lo rifa':

```
[wws-state] loadState = 0x00900001   <- header del job, stato 1
[wws-state] loadState = 0x00000002   <- esegui i comandi
[wws-state] loadState = 0x00000003   <- pronto
[wws-state] runJob    = 0x00000000   <- gira
[wws-state] storeJob  = 0x00000000   <- store
[wws-state] storeJob  = 0xFFFFFFFF   <- fatto, e si ricomincia
```

Due corse su due identiche.

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
