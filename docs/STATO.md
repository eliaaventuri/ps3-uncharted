# Stato del progetto

Ultimo aggiornamento: **20 settembre 2026** — sessione 39.

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
in giallo. Poi il remap dei canali delle texture (SET_TEXTURE_CONTROL1,
l'atlante dei caratteri tiene 4 pagine di glifi nei 4 canali) e il menu si
legge: **"New Game / Options", "(X) SELECT"**. `cellSaveDataListSave2` con le
callback guest: "New Game" salva (`gamedata/dev_hdd0/.../BCJS30015_NDI_
UNCHARTED_DF_0`) e parte `dive-boat-movie-intro`: il livello `dive-boat` si
carica con tutti gli attori (pirati, animazioni, banco audio). Altro bug del
lifter PPU trovato li': `vminfp/vmaxfp` e diverse op intere VMX leggevano il
register file big-endian con cast host (il quantizzatore della navmesh
invertiva min/max e il PPU finiva nello stack). Fronte aperto: nel livello i
job SPU di rendering (ovl 218 = `jobmod_00903d80`) scrivono a LS 0 attraverso
un puntatore nullo letto dai propri dati, e la scena 3D non si vede ancora
(nero: i buchi dei draw nei buffer SPU restano NOP). Tastiera = pad 0 nella
finestra (frecce, Spazio/X = CROSS, C = CERCHIO, Invio = START); `PAD_SCRIPT`
per le corse senza nessuno. Dettagli in "Sessione 27".

**Dalla sessione 40 le texture compresse restano compresse.** Le BC1/2/3
(DXT) venivano decodificate in RGBA8 sul thread del walker: al caricamento
del livello la cache saliva a 12,7 GB di texture Metal (footprint di picco
14-15 GB), in gioco 250 MB. Ora vanno alla GPU come blocchi BC: picco 2,0 GB
di texture (footprint 3,3 GB), 50 MB in gioco, caricamento del livello ~8 s
piu' corto, immagine identica. Le corse "cattive" (poligoni giganti, cielo
e mare spariti) NON dipendono da questo: compaiono con e senza BC quando il
gioco entra nell'incontro `dive-boat-encounter-first-wave` (13/29 e 17/29
screenshot difettosi contro 0-3/29 nelle corse in cui Drake resta fuori
dall'incontro). E' il fronte aperto. Dettagli in "Sessione 40".

**Dalla sessione 39 la geometria grande non sparisce piu' a fotogrammi
alterni.** Il flip si diceva concluso al massimo 8 ms dopo la richiesta anche
se la GPU non aveva finito il fotogramma: il job di culling del titolo leggeva
i report delle occlusion query ancora a zero e tagliava ponte, mare e cielo
(7 screenshot su 10 con la scena mezza vuota, poligoni giganti, esposizione a
bianco; era cosi' da almeno la s.37, nessuno aveva guardato abbastanza
fotogrammi). Ora il gestore del flip resta "dovuto" finche' il completed
handler della GPU non lo sblocca: 27 screenshot puliti su 28. Il present non
copia piu' gli attributi (anello NoCopy): 4,2 -> 2,0 ms. Dettagli in
"Sessione 39".

**Dalla sessione 38 il livello gira a 27-28 fps** (era 12,6 sulla stessa
corsa di riferimento): il nostro kernel SPURS faceva fork-join per ogni
quanto (sveglia 5 lane, aspetta la piu' lenta, un workload alla volta); ora
ogni lane sceglie da sola il workload, come le SPU vere
(`SPURS_LANE_SCHED=1` in `run.sh`) -> 24-26,6 fps. Poi il fetch dei vertici
del backend Metal con un piano per draw (equivalenza provata su 3,8
miliardi di fetch) -> 28. Le lane sono all'87%, vicine alla parita' con le
SPU vere; il titolo e' un gioco a 30 fps. Dettagli in "Sessione 38".

**Dalla sessione 37 il livello gira a 13-14,5 fps** (era 10,7-11,9 sulla
stessa finestra di orologio): `cellGcmSetWaitFlip` bloccava il PPU fino al
flip (64 ms di tetto), ma sul ferro e' un'attesa per la RSX dentro la FIFO e
il PPU non si ferma. Bloccandolo, la fase PPU (31 ms) e quella del walker
(46-57 ms) andavano in fila; ora si sovrappongono e il gioco si sincronizza
da solo sul registro ref (`GCM_WAITFLIP_BLOCK=0` in `run.sh`). Il flip che
il walker raggiunge ma il tick non fa in tempo a chiudere si chiude alla
richiesta successiva (stato DONE + gestore). La via in linea degli accessi
alla memoria del PPU liftato e' fatta (`PPU_VM_INLINE` in `ppu_recomp.h`)
ma misurata neutra: il PPU non e' sul cammino critico. Screenshot
`docs/img_s37_dive_boat_waitflip_nb.jpg`. Dettagli in "Sessione 37".

**Dalla sessione 36 il livello gira a 10,4-10,6 fps** (era 8,0-8,6): via il
ramo delle sonde negli accessi alla LS (il compilatore scaricava tutti i
registri a ogni accesso), FPCR delle lane come la SPU (denormali a zero,
troncamento) con la via veloce dei float che guarda solo il risultato, e
`cntb`/byte-shift/confronti/conversioni in NEON. Il lifter PPU emette le
barriere di memoria (`sync/lwsync/eieio/isync` erano no-op) e
`ShutdownWorkload`/`WaitForWorkloadShutdown` aspettano davvero il quanto
del PM in volo. Dettagli in "Sessione 36".

**Dalla sessione 35 Drake puo' morire e il gioco riparte dal checkpoint.**
Lo stallo intermittente dopo la morte (e quando Drake parlava) era il
workload SPURS wid 3 (animazione/ragdoll, dichiarato a UNA SPU) che il
nostro kernel faceva girare su 6 lane: i job si sovrascrivevano il flag di
"fatto" e il PPU aspettava per sempre. Ora `SPURS_FORCE_SPUS` vale solo per
i workload multi-SPU: 3 morti su 3 seguite dal ricaricamento del checkpoint.
Dettagli in "Sessione 35".

**Dalla sessione 33 Drake e' intero** (`docs/img_s33_drake_intero.jpg`):
braccia e gambe della muta c'erano sempre state, ma le matrici delle
giunture "twist" arrivavano allo skinning come riempimento 0xFFFF0000/
0x7FFF0000, che per la SPU e' un numero enorme ma finito (nessun NaN sul
Cell) e per l'host e' NaN: peso 0 x enorme = 0 sul ferro, NaN da noi, e i
vertici sparivano. Gli helper SPU in virgola mobile ora seguono la specifica
CBEA (niente NaN/infinito, denormali = 0, troncamento, saturazione).
Dettagli in "Sessione 33".

**Dalla sessione 30 il livello si vede come nel gioco** (`docs/img_s30_dive_boat_colormask.png`):
cielo intero, gru, sartie, mare, casse, HUD; i "triangoli neri" erano le
parole `DRAW_ARRAYS` da 256 vertici mandate al backend una per una (una
lista di triangoli ripartiva sfalsata di un vertice), la sfera e la cupola
nere erano i volumi delle luci differite disegnati senza maschera di
colore. Risolti anche il crash del present (viste con remap senza
proprietario a tabella piena), i draw scartati dal tetto dei vertici e due
fragment program oltre i 4 KB. Fronte: il personaggio (Drake) esce a pezzi
staccati e scuro -- lo skinning. Dettagli in "Sessione 30".

**Dalla sessione 29 il livello si vede a schermo, con l'HUD.**
`docs/img_s29_dive_boat_display.png`: il ponte della dive-boat con casse e
pavimento texturizzati, cielo con nuvole, "1 Ambushed", "Press (O) to take
cover when next to an object", il contatore "40". Quattro difetti in fila
(dettagli in "Sessione 29"): le costanti dei fragment program fuori
dall'hash della cache (da 8192 "programmi" a ~120), la copia
memoria-a-memoria della RSX (NV0039) con cui il titolo compatta il proprio
heap texture (senza, cielo/pavimento/sfondo di pausa a rumore), le texture
in streaming che la cache serviva stantie dal primo bind, e le normali
compresse (tipo CMP) non decodificate. Restano triangoli neri in alto a
sinistra, superfici verdi piatte a destra, ~1.000 draw per fotogramma
scartati dal tetto `MTL_MAX_VERTS`, ~8 fps.

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

### Fronte aperto (dopo la sessione 40)

1. Le corse cattive nell'incontro: poligoni giganti + geometria grande
   sparita (punto 2). Prima cosa: riprodurre a comando (PAD_SCRIPT), poi
   `RSX_GARBAGE` per capire quali draw hanno i vertici sbagliati.
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

## Sessione 37 — cellGcmSetWaitFlip non blocca piu' il PPU: 10,7-11,9 -> 13-14,5 fps; via in linea degli accessi PPU (neutra)

**Misura nuova, prima di tutto.** La finestra f#2300-2500 non e' piu'
confrontabile fra corse a fps diversi (f#2300 cade in un momento di gioco
diverso). Ora si usa la mediana del tempo per fotogramma nei blocchi
`RSX_FRAMESTAT=50` fra 100 e 190 s di orologio dal primo fotogramma
(`scratchpad/misura/fpsmed.sh <log>`); il gioco avanza a tempo reale,
quindi la finestra copre lo stesso contenuto. Rumore fra corse identiche:
+-0,5 fps (A1/A2 del binario s.36: 11,9/10,9). Gli altri script in
`scratchpad/misura/`: `fps.sh` (finestra f#), `fpswall.sh`, `tl.py`/`tl2.py`/
`tl3.py` (timeline `GCM_TIMELINE`: attesa del flip, fase PPU, put->put),
`runshots.sh` (corsa con screenshot a 95/110/125 s), `prof2.sh` (`sample` a
100 s). `winid.swift` sta in `scratchpad/`, non in `tools/`.

**1. Via in linea degli accessi alla memoria guest del PPU (fatta, neutra).**
`tools/ppu_lifter.py`: i sorgenti liftati definiscono `PPU_VM_INLINE` e il
header generato (`ppu_recomp.h`) definisce `vm_read8/16/32/64` e
`vm_write*` come `static inline` (solo `__GNUC__`; MSVC e il runtime vedono
le dichiarazioni di prima): limiti (`ua <= ppu_vm_size - n`, che con
`ppu_vm_size == 0` di Win32 manda fuori linea solo gli ultimi n byte),
poll del registro ref (`ppu_hle_inject_base + 0x2008` -> fuori linea),
rilevatore di spin (`g_vm_spin` globale, a 4096 letture uguali -> fuori
linea, che pompa i gestori), `put` (`g_gcm_put_watch_addr` -> fuori linea),
poi `memcpy` + `bswap`. Tutto il resto passa da `vm_*_ool` in
`ppu_loader.cpp` (che chiamano le funzioni di prima; `g_vm_fast` e
`g_vm_spin` ora esportati). Lift nuovo identico al vecchio a meno del
preambolo (1 riga per chunk + header); istruzioni host per chunk
raddoppiate (2,4M -> 4,9M nel chunk 003); build -O2 2:48. **A/B a parita'
di tutto il resto (modalita' bloccante): s.36 11,9/10,9 fps mediani, in
linea 11,4/11,4 -- neutra.** Il profilo spiega perche': il thread PPU
principale sta il 38% del tempo a lavorare e il 62% in `cellGcmSetWaitFlip`
-> `nanosleep` (era il punto 1 del fronte s.36: sbagliato, il PPU non era
il collo). Tenuta perche' corretta e gratuita; `PS3_VM_FAST=0` la spegne
(tutto fuori linea). Backup: `uncharted-data/recomp.bak_s36`,
`obj/o2_s36_backup/` (con il binario `uncharted` s.36).

**2. Il vero collo: cellGcmSetWaitFlip bloccava il PPU.** Timeline
(`GCM_TIMELINE`, `tl.py`) a f#2300+: fotogramma 89 ms = attesa del flip 57
ms (put -> waitflip-out; il tetto di 64 ms scattava in 22 casi su 672) +
fase PPU 31 ms (waitflip-out -> put successivo); il walker finiva a 46 ms
dal put, il flip si chiudeva al tick dopo il present (+11 ms). Cioe' PPU e
walker IN FILA. Sul ferro `cellGcmSetWaitFlip` scrive nella FIFO un'attesa
per la RSX (niente comandi dopo finche' il flip non e' avvenuto) e torna
subito; il PPU aspetta altrove (Uncharted: sul registro ref, `[WAIT]`).
Il commento in `cellGcmSys.c` diceva che Uncharted la chiama una volta
sola al boot (s.32): falso nel livello, la chiama a OGNI fotogramma subito
dopo il put (2.964 volte in 130 s). Con `GCM_WAITFLIP_MS=1` (prova):
14,6 fps a f#2300-2500, immagine giusta. Fatto bene: `GCM_WAITFLIP_BLOCK=0`
(nuovo, `run.sh`; il default nel codice resta il blocco per gli altri
titoli, che ci contano per il ritmo) -> `cellGcmSetWaitFlip` torna subito
senza toccare lo stato del flip (evento `waitflip-nb`).

**3. Il flip perso.** Senza il blocco, il titolo chiede il flip N+1 200 us
dopo che il walker ha raggiunto il put di N (`walk` -> `put`: 0,2 ms), prima
del tick (16 ms) che avrebbe chiuso il flip N; `cellGcmSetFlipCommand`
azzerava `s_flip_reached` e il flip N spariva: **0 `flip-done` in 200
fotogrammi**, stato WAITING per sempre, gestore del flip mai chiamato (il
gioco andava lo stesso, 14,45 fps mediani, ma non e' il ferro). Ora in
`cellGcmSetFlipCommand`: se il flip precedente e' stato raggiunto dal
walker ed e' ancora WAITING, si chiude li' (`flip-done-late`: stessa
attesa della GPU del tick, stato DONE prima del WAITING nuovo, gestore
subito con `ppu_gcm_pump()`; bit 4 della pompa = solo gestore, senza
toccare lo stato). 200 `flip-done-late` su 200 fotogrammi. Costa ~1 fps
(13,7/13,2 contro 14,45 senza; `GCM_FLIP_LATE=0` per l'A/B), i draw per
fotogramma sono gli stessi (~1.800): la differenza non e' spiegata, forse
rumore + le morti (3 in nb1 e nb3, 0 in nb2).

**Risultato: 13,2-13,7 fps mediani (100-190 s) contro 10,7-11,9; 4 corse
da 200 s senza stallo ne' assert (nb1-3 + wf1), morti seguite dal
checkpoint, immagine identica alla s.36** (`docs/img_s37_dive_boat_
waitflip_nb.jpg`; il fotogramma scuro a 110 s in nb1 era Drake quasi
morto, l'effetto di salute bassa del gioco). Per fotogramma ora: put ->
walk 65-78 ms, walk -> put 0,2 ms: **il fotogramma e' il walker che aspetta
i job SPU** (12 park), con il PPU in parallelo (spin sulla job list
`func_0074232C`, 38%).

**Profilo di oggi (`sample`, livello):** thread kernel SPURS 56% occupato,
6 lane 37-42%, PPU principale 38%, ticker 32% (24% walker + 8% present):
nessuno saturo. Il simbolo piu' caldo di tutti e' `jobmod_008dc900_..._
spu_func_00010300` (job 215, blocchi 0x10300-0x12100, il ciclo caldo di
0x11298 della s.34): 810 campioni contro 226 del secondo. 348 `spu_ls_
read128` + 191 write + 183 `shufb` + 165 `selb` in 2.077 righe.

### Fronte aperto (dopo la sessione 37)

1. Velocita' (13-14 fps): il fotogramma e' il walker che aspetta i job SPU
   del wid 12 (215/218) -- nessun thread saturo, quindi e' LATENZA della
   catena PPU -> kernel SPURS -> lane -> walker, non throughput. Da
   guardare: (a) il thread kernel SPURS al 56%, che esegue anche i job sul
   proprio thread (`spu_run_policy_module` -> `jobmod_008dc900`), (b) i
   park del walker (12 per fotogramma: quanto aspetta ognuno, e se il job
   che sblocca era gia' in coda), (c) `spu_func_00010300` del job 215
   (cache dei registri con analisi di flusso, o u128 in ordine SPU).
2. Perche' chiudere il flip (gestore + DONE) costa ~1 fps rispetto a non
   chiuderlo mai: cosa fa il gestore del flip del titolo.
3. Giocare oltre il checkpoint (Elia con la tastiera, o `PAD_SCRIPT`).
4. `RING_GUARD_LANES` 5, death4 (mai piu' visto).

## Sessione 36 — barriere di memoria nel lifter PPU; ShutdownWorkload/WaitForWorkloadShutdown veri

**Barriere (fronte 2 della s.35, fatto).** `tools/ppu_lifter.py` emetteva
`sync/lwsync/eieio/isync/ptesync` come no-op (2.439 siti nell'ELF): su
ARM64, ordinato debolmente come il PPU, le lane SPU e gli altri thread PPU
potevano vedere i dati di un job DOPO la parola che li pubblica. Ora:
`sync`/`lwsync`/`ptesync` -> `std::atomic_thread_fence(seq_cst)` (il
disassemblatore piega lwsync in "sync"), `eieio` -> acq_rel, `isync` ->
acquire; `#include <atomic>` nel preambolo dei sorgenti generati (sono
C++). Le istruzioni di cache (`dcbt/dcbf/icbi/...`) restano no-op. Il
lift nuovo e' identico al vecchio a meno di quelle righe (verificato con
diff a righe filtrate); ricompilato a -O2 (6 min). Costo: zero misurabile
-- f1/f2/f3: 8,6/8,0/8,0 fps nella finestra f#2300-2500 (s.35: 8,7), 3
corse da 200 s senza stallo, 1 morte (f3) seguita dal ricaricamento del
checkpoint e da altri 80 s di gioco a 8-9 fps. Backup del lift e degli
oggetti precedenti: `uncharted-data/recomp.bak_s35`, `obj/o2_s35_backup/`.

**ShutdownWorkload/WaitForWorkloadShutdown veri (fronte 3 della s.35,
fatto).** Erano no-op: `RemoveWorkload(12)` tirava via il workload mentre
il thread kernel aveva ancora un quanto del PM in volo (lane che finivano
l'ultimo giro), e il PPU riciclava la job list sotto un job in corsa
(death4 in s.35: assert `stopd @0x0423C` "slot della job list non
vuoto", 1 corsa su 12). Ora in `libs/spurs/cellSpurs.c`:
`ShutdownWorkload` alza `s_workloads[wid].shutdown`; il thread kernel
segna `s_wkl_running[wid]` prima di far partire le lane e non fa partire
quanti nuovi se `shutdown` e' alzato (protocollo di Dekker con gli
`__atomic` seq_cst); `WaitForWorkloadShutdown` e `RemoveWorkload`
aspettano che `s_wkl_running` torni a 0 (tetto `SPURS_SHUTDOWN_WAIT_MS`,
default 500, con avviso "ancora in volo": mai un blocco); `AddWorkload`
azzera il flag. Misurato su 3 corse da 200 s (sd1-3): l'attesa scatta di
rado (<1000 su ~2.200 Shutdown per corsa) e dura 0,08 ms in media, mai
il tetto; fps 8,0/7,9/8,0 (f#2300-2500), 0 stalli, 1 morte (sd3) con
ricaricamento del checkpoint.

**Velocita': da 8,0-8,6 a 10,4-10,6 fps (f#2300-2500; 11,7-13,4 fps a
f#3000-3400, finestra che dipende da cosa succede nel gioco), stessa
immagine (`docs/img_s36_dive_boat_neon_fz_rz.jpg`), 5 corse da 200 s senza
stallo ne' assert, 1 morte col checkpoint.** In ordine di resa:

1. **`-DSPU_LS_NODIAG` (default in `build.sh`, +10%: 9,4 -> 10,3).** Gli
   accessori della LS (`spu_ls_read128/write128`) testavano `g_spu_ls_diag`
   a ogni accesso e sul ramo lento chiamavano una funzione fuori linea: per
   il compilatore quella chiamata puo' toccare tutto, quindi OGNI registro
   guest tenuto in un registro host andava scaricato in `ctx->gpr` prima e
   riletto dopo -- il corpo liftato era una sequenza di load/store. Con la
   macro il ramo sparisce. Le sonde `SPU_LS_*` (watch, LOWREAD, ...) non
   funzionano piu' nella build di default: `SPU_DEFS=" " ./build.sh` le
   riaccende (`obj/spu_defs.stamp` forza il rifacimento degli oggetti SPU
   quando le define cambiano).
2. **FPCR delle lane = FZ + arrotondamento verso lo zero** (`spu_host_fp_
   enter/leave` in `spu_context.h`, chiamati da `spu_run_with_halt`,
   `SPU_FP_HOST=1` per l'A/B). La SPU non ha denormali e tronca sempre: con
   FZ e RZ le istruzioni NEON danno il risultato della SPU e la via veloce
   di fa/fs/fm/fma/fms/fnms guarda SOLO il risultato (`|w| >= 0x7F7FFFFF`
   -> via esatta in double): un ingresso esteso (per il ferro inf/NaN) non
   da' mai un risultato finito, quindi si vede sempre nel risultato. Prima:
   3 flush + 4 controlli per operazione (`spu__flush4`/`spu__noext4` erano
   il 10% del tempo delle lane nel profilo a -fno-inline; `spu__f2d` un
   altro 5%: la via lenta scatta spesso perche' la tavolozza di skinning e'
   piena di sentinelle 0x7FFF0000, s.33). Verificato con un differenziale
   da 200.000 prove contro la via esatta (ingressi estesi, denormali, zeri,
   massimo normale): 0 discrepanze. `spu_fm/fa/fs_slow` ora fuori linea
   (la via lenta era copiata in ogni sito: 139 istruzioni per un fm).
3. **Helper in NEON** (`runtime/spu/spu_helpers_neon.h`, tabelle in
   `spu_neon_tables.h` generate da `tools/gen_spu_neon_tables.py`; le
   versioni scalari restano sotto `#ifndef SPU_NEON_*` per gli altri host):
   `cntb` (era un ciclo bit a bit su 16 byte: `vcntq_u8`), `rotqby/shlqby/
   rotqmby` e varianti `bi`/immediate (cicli di byte con SPU_W: ora un
   `vqtbl1q_u8` con una riga di tabella per scorrimento, 0xFF = zero),
   `cbd/chd/cwd/cdd_pos` (tabella di 16 controlli), `fcgt/fcmgt` (via double
   per corsia: ora confronto intero segno-modulo, esatto anche per gli
   estesi -- RPCS3 li "abbassa" al massimo normale e per ext vs 0x7F7FFFFF
   risponde al contrario del ferro), `cflts/cfltu/csflt/cuflt`
   (`vcvtq_s32_f32` tronca e satura come la SPU; estesi saturati per segno).
   Differenziale NEON/scalare: 0 discrepanze su 200.000 prove.

4. **`spu_shufb` senza inversioni (10,3 -> 10,6 fps).** Il byte host h del
   controllo c E' il controllo del byte host h del risultato (stesso
   SPU_W), e il byte SPU s del concatenato {a,b} sta nel byte host s ^ 3:
   indice = (c & 0x1F) ^ 3 su {a,b} cosi' come sono, casi speciali letti
   da c cosi' com'e'. Da 4 vrev32 + 8 op a 8 op. Differenziale 300.000
   prove: 0 discrepanze.
5. Anche `ceq/cgt/clgt` (parola, halfword, byte), `absdb/avgb/sumb`,
   `shl/shlh/rot/roth/rotm/rotma/rothm/rothma` (vshlq con quantita' per
   corsia, negativa = destra, oltre la larghezza = 0/segno come la SPU) e
   `fsmb/fsmbi` in NEON: corretti (differenziale 0 su 300.000) ma dentro il
   rumore (10,4 fps): erano piccoli anche prima.

**Come si e' misurato:** i due moduli caldi (215 geometria, 218 tavolozza)
compilati con `-fno-inline` e `sample` per 10 s nel livello (fps 5,5 cosi',
ma le proporzioni si vedono): helper 3.683 campioni, corpi liftati 2.837,
accessi LS 664; in testa `spu_shufb` 590, `spu__noext4` 414, `spu__f2d`
373, `spu__flush4` 307, `spu_rotqby` 224, `spu_rotqbyi` 212, `spu_cntb`
152, `spu_a` 141, `spu_selb` 135, `spu_shlhi` 103, `spu_fcgt` 82. Il
`vm_read32` a 726 e' il PPU che aspetta la job list (`func_0074232C`), non
lavoro. Costo statico per istruzione SPU sul ferro (istruzioni host,
`clang -O2`): a 15, ai 11, selb 5, shufb 21, rotqby 102, rotqbyi 4,
shlqbyi 25, fm 139 (con la via lenta in linea), fma 61, lqd 30, stqd 39.

**Fallito: cache dei registri nel lifter SPU (`--reg-cache`, tenuto come
opzione, spento).** I registri di ogni funzione liftata come variabili
locali `u128 rN`, scritte in `ctx->gpr` (RC_FLUSH) prima di uscite,
chiamate, canali e stop, rilette (RC_LOAD) dopo; solo per le funzioni con
>= 24 istruzioni (`--reg-cache-min`). Corretto (stessa corsa, 0 assert) ma
**6,9 fps contro 8,0**: la funzione 0x4090 del job 218 passa da 13,6k a
23,4k istruzioni host, load 2.499 -> 8.654, store 1.069 -> 3.294. Cause:
100+ variabili vive (spill), e a ogni punto di sincronizzazione (121 nella
sola 0x4090) si scaricano/ricaricano TUTTI i registri usati. Per farla
rendere servono le analisi di flusso (registri sporchi dal'ultimo sync in
avanti, registri vivi dopo il sync all'indietro): non fatto. Build a
-O2 con la cache: 19 minuti (13 clang paralleli) contro ~2. I sorgenti
liftati senza l'opzione sono identici a prima (verificato con diff).
Lift con la cache conservato in `uncharted-data/spu_jobs/lifted_rc_s36`.

**Strumenti nuovi:** `tools/winid.swift` (`swiftc -O tools/winid.swift -o /tmp/winid`; `/tmp/winid
uncharted` da' l'id della finestra per `screencapture -x -l <id>`; la
finestra NON e' "on screen" per CGWindowList, serve `.optionAll`).

### Fronte aperto (dopo la sessione 36)

1. Velocita' (10,4-10,6 fps): gli helper sono quasi tutti NEON ora; quel
   che resta e' il corpo liftato (un load+store di ctx->gpr per istruzione,
   piu' rev32 a ogni lqd/stqd) -> la cache dei registri con le analisi di
   flusso (sporchi/vivi ai punti di sync), oppure il profilo a -fno-inline
   rifatto per vedere cosa e' salito in testa. Poi i 41-46 ms di PPU da
   solo per fotogramma: OGNI load/store del PPU liftato e' una chiamata
   fuori linea (`vm_read32` in `ppu_loader.cpp`: test g_vm_fast, oob,
   controllo del registro ref per GCM_REFPOLL, rilevatore di spin in TLS
   con `ppu_gcm_pump` ogni 4096 letture uguali, memcpy+bswap), e attorno a
   una chiamata il compilatore scarica i registri. Una via in linea nel
   header generato (accesso diretto per gli indirizzi fuori dalla finestra
   HLE, chiamata per il resto) e' la leva; attenzione al pump: serve dove
   il PPU gira su una parola che scrive un gestore vblank/flip.
2. Giocare oltre il checkpoint (Elia con la tastiera, o `PAD_SCRIPT`).
3. `RING_GUARD_LANES` 5 (la sesta lane del wid 12 senza guardia), death4
   (mai piu' visto in 9 corse dopo lo shutdown vero: da confermare).

## Sessione 35 — lo stallo alla morte di Drake era il wid 3 (lista a UNA SPU) girato su 6 lane

**Trovato e corretto: 3 morti su 3 (fix16, fix18 x2) seguite dal ricaricamento
del checkpoint e 90 s di gioco, contro 2 stalli su 3 morti prima (stab6,
fix8; fix7 era passata per fortuna). fps nel livello invariati (8,3 nella
finestra f#2300-2500).** Non era il walker
ne' `RemoveWorkload`: dopo `kPlayerDied` il PPU chiama `func_005708E8`
(manda un job SPU e ne aspetta il flag: `func_00571848`, ciclo a 0x571B00
`while (!(flag & 1))`) sul workload **wid 3** (`BasicJobList`, `game/
spumanager.cpp:701`, job list a 0x2A8D2900, job module 208 poi 211). Il
titolo registra il wid 3 con **maxContention = 1** (una SPU), ma il nostro
kernel applicava `SPURS_FORCE_SPUS=6` a TUTTI i workload: i job della lista
(6 blocchi di parametri a 0x2B14B280+0x10*n, tutti a leggere-modificare-
scrivere gli stessi buffer 0x2B14BC00/0x2B14FC00/0x2B14B970) giravano in
parallelo su 3+ lane (`SPU_DMA_EAWIN_OVL=208`: contesti cF910/cF8CC/c053C
sugli stessi EA). L'ultimo job scrive il marcatore "fatto" (16 byte a
0x2B14BC00, pc 0x4434), un altro job in corsa riscrive il buffer intero con
la copia vecchia (pc 0x2B58, 0x4000 byte da 0x2B14BC00) e il flag sparisce:
il PPU gira per sempre, la FIFO si svuota, i fotogrammi si fermano. Il
kick del wid 3 arriva alla morte di Drake (ragdoll) ma anche quando Drake
PARLA ("Setting facial speech"): e' lo stesso stallo di death6 (senza
morte, a 120 s) e di w3_10 (a 125 s), e spiega perche' era intermittente.
Contention dei workload (nuovo nel log di `AddWorkload`: `minC/maxC`):
wid 0/1/6/8/11 = 5, wid 5 = 8, **wid 2/3/4/7/9/10 = 1**.
Fix in `libs/spurs/cellSpurs.c`: `SPURS_FORCE_SPUS` vale solo per i
workload con maxC > 1 (il wid 12 resta a 6 lane).

**Secondo difetto trovato per strada (fix8/death5, stallo all'ingresso del
livello, f#2310):** il watchdog delle DMA identiche (`SPU_DMA_REPEAT_LIMIT`
200.000) uccideva il job di geometria (ovl 215, pc 0x4B10) mentre
interrogava il label RSX 0x50000490: con il walker indietro e la guardia
dell'anello che trattiene la lane (valvola a 2 s) le letture identiche
superano 200.000 in meno di 2 s. Job ucciso = pacchetto col JUMP-a-se-
stessi mai patchato = walker parcheggiato per sempre (a 0A42CA90) = le
altre lane nella guardia, valvole ogni 2 s (15 righe), PPU in
`func_0074232C`/`func_007428C0`/`func_0074073C` (attesa della job list:
min dei ticket per SPU a +0x12..+0x1E dell'intestazione >= n).
Fix in `runtime/spu/spu_dma.h`: i poll dei label RSX (0x50000480-0x500004FF,
GET da 4 byte) non contano per il watchdog.

**Strumenti nuovi:**
- `[park-watchdog]` (`GCM_PARK_DUMP_MS`, default 5000): fermi sullo stesso
  park da piu' di N ms -> parole attorno, generazioni parkgen, stato della
  guardia dell'anello per lane (`rsx_ring_guard_dump`), job list viva del
  wid 12 (`cellSpurs_dump_wid_joblist`, hook registrato da cellSpurs.c
  perche' il target `ps3recomp_host` non linka cellSpurs).
- `SPURS_DISPATCH_TRACE_WID=<n>`: il trace dei dispatch solo per quel wid.
- `PAD_STICK_FROM=<s>`: `PAD_STICK`/`PAD_SWEEP` solo da quel secondo (prima
  lo stick muoveva la selezione nel menu); `PAD_SWEEP` ora a tempo di
  parete (era `clock()`, CPU: il passo cambiava a caso).
- `ShutdownWorkload`/`WaitForWorkloadShutdown` stampano il conteggio ogni
  500 chiamate (il titolo li chiama a ogni fotogramma: #2500 a f#2600).
- `scratchpad/stallcatch2.sh <log> <s> [VAR=..]`: corsa che campiona il
  processo (`sample`) se i fotogrammi si fermano 30 s; `deathcatch.sh`
  idem ma solo dopo `kPlayerDied`. (lldb `-p` resta appeso all'attach:
  non usarlo dagli script.)

**Riproduzione della morte:** senza tasti Drake muore a **~130 s dal
lancio** (95 s con il boot veloce) in 5 corse su 8 (stab6, fix7, fix8,
fix16, fix18 due volte; no in fix13/14/15/17), all'ondata dei pirati; con
`PAD_STICK`/`PAD_SWEEP`/salti non muore (si sposta dalla linea di tiro).
**Boot:** con i workload a una lane il boot e' a volte piu' veloce e
l'impulso START dell'autopress finiva sul menu principale gia' interattivo:
"New Game" non partiva (2 corse su 3, 0 su 3 con `SPURS_FORCE_SPUS_ALL=1`
= comportamento vecchio). Ora `run.sh` mette `LBP_AUTOPRESS_NOSTART=1`
(solo CROSS; "Press START" accetta CROSS): 4 boot su 4 + 3 su 3. Dopo la morte corretta (fix7) il
gioco RICARICA IL CHECKPOINT: `GameTaskSubnodeReset 'dive-boat-encounter-
first-wave'`, respawn delle entita', fotogrammi a 8-9 fps: e' il primo
checkpoint ricaricato dal port.

**Worker Gemini:** morto dopo aver letto 4 file (`Error: Requests ending
with a model turn are not supported`), come in s.34 (appeso): le tre
funzioni del poll le ho lette a mano (50-70 righe l'una).

**Non risolto / visto:** (a) la lifter emette `sync/lwsync/eieio` come
no-op (2.439 siti): su ARM64 e' un buco di ordinamento fra PPU e lane; i
kick via `stwcx.` (CAS seq_cst) sono ordinati, gli store semplici no.
Da fare: `__atomic_thread_fence` nel lifter e relift. (b) death4: assert
`stopd @0x0423C` nel job 217 (slot della job list non vuoto: un job
scrive un job header a joblist+0x20+8*idx e pretende lo slot a zero)
fra `RemoveWorkload(12)` e `AddWorkload(12)`, 1 corsa su 12, stallo a
f#720; probabilmente lo stesso problema di contention (wid 217 = ?) o la
mancanza di `WaitForWorkloadShutdown` (no-op: il PPU ricicla la lista
mentre una lane finisce il job precedente). (c) `RING_GUARD_LANES` e' 5:
la sesta lane del wid 12 non ha guardia (label 0x4D0).

### Fronte aperto (dopo la sessione 35)

1. Verificare il fix del wid 3 su piu' corse (morte a 130 s + Drake che
   parla) e poi giocare oltre il checkpoint.
2. Barriere di memoria nel lifter PPU (sync/lwsync/eieio -> fence) e
   relift a -O2.
3. `WaitForWorkloadShutdown` vera (aspettare il quantum in volo) e
   `RemoveWorkload` che aspetta: chiude la corsa di death4.
4. Velocita' (8,2-8,8 fps), tonalita' vs RPCS3, IFE/LOOP nel decompilatore
   FP, `spu_fi`: come in s.34.

## Sessione 34 — la tonalita' e' deterministica: il fotogramma "slavato" era l'effetto danno del gioco

**Fronte 1 chiuso: 5 corse su 5 allo stesso fotogramma 2450 danno la stessa
immagine** (media RGB del drawable 128-135 / 120-125 / 108-112; c1-c4 di oggi
piu' il fotogramma delle 06:05). Il caso "slavato" di c62 non e' del nostro
renderer: nel suo drawlist la catena di post-processing su 031E0000 usa
SHADER DIVERSI (62ECDDEF, 68D8AE17, DEC27447 x3, B9FEED90, 753654F2 x13) da
quelli di c63 e di tutte le corse di oggi (1ACB4037, 248E54C4, 46B7C135,
7E141F22 x3, 7F519531, EBF9BB74, 753654F2 x6, piu' D5FE4484 -> 025D0000/
033C0000), e 7E141F22 finisce su 016D0000 invece che su 031E0000: e' un altro
stato del gioco (l'effetto "colpito/quasi morto" di Uncharted desatura e
sbianca lo schermo; i pirati sparano e il colpo dipende dal tempo). Quindi
per confrontare la tonalita' con RPCS3 serve uno scatto in stato pulito da
entrambe le parti: quello di s.31 (`docs/img_s31_rpcs3_dive_boat.jpg`, grigio
e velato) potrebbe essere nello stesso stato "colpito".

**La composizione finale (fp A310E8C2, variante 00BAFDB5 in altre corse: il
display alterna 00050000/00410000)** letta in MSL (`RSX_FP_DUMP=A310E8C2`):
tex0 = scena HDR 016D0000, tex1 = quadrante 320x180@032C1000 (sfocato: DOF,
pesato con la profondita' tex3 02D50000 e c0..c8), tex2 = bloom 160x90
031B0000 moltiplicato per la sua alpha, poi `scena * c10.x + bloom`, poi
`* c11 + c9` (esposizione e correzione colore: COSTANTI dal PPU, nessuna
texture di luminanza). Non legge tex14/tex15. La catena della log-luminanza
(03120000 -> 03EB0000 40x23 -> 03EC0000 10x6 -> 03ED0000 3x2 -> 00030000
1x1, valori ~-1.3 = log della media) resta senza lettori noti; il titolo ha
un'esposizione automatica ("Scene Luminance = (meas)", "tonemap-adaption-time",
"tonemap-exposure-method"), ma nel livello la tonalita' e' stabile, quindi o
il metodo e' manuale o l'adattamento e' gia' a regime. Se la tonalita'
tornasse a cambiare fra corse: `RSX_DRAWLIST_FPC=A310E8C2` stampa c9/c10/c11.
Nessuno dei 306 fragment program del livello legge l'ingresso FOGC
(`RSX_FP_DUMP=1`, `grep input.fog` = 0): la foschia e' nei materiali.

**Corretto: le texture float a 32 bit (formato 0x9B, W32_Z32_Y32_X32_FLOAT)
cadevano nel default "R8".** Uncharted lega una 4x1 fp32 in memoria
principale (@118800/118880/118900, tex15 della composizione e di altri pass:
parametri, es. `(-0.063 -0.339 -0.063 0.288) (-0.034 0.510 -0.034 0.410)
(-0.015 0.237 -0.015 0.960)`) che usciva rumore. Ora `hdrb` = 8 (0x9A) o 16
(0x9B) byte per texel, swap a 32 bit, texture Metal RGBA32Float; le prime 8
decodifiche di texture fp32 fino a 4 texel vengono stampate. Il fotogramma
2450 non cambia (media 128.8/120.9/110.4): chi la legge non e' sul percorso
principale.

**Strumento nuovo: `[surfstat]`** nel dump delle superfici (`RSX_DRAWLIST=n
RSX_DUMP_FRAME=100000`): per ogni superficie FP16/FP32 media e massimo per
canale e conteggio dei NaN sui valori VERI (il .ppm li taglia a [0,1]), e le
superfici fino a 16 texel per intero. Numeri del livello (f#2450): scena HDR
016D0000 media 0.53/0.49/0.43 max ~20; 025D0000 = copia a mezza risoluzione
nel quadrante in alto a sinistra (media = 1/4 esatto); bloom 031B0000 media
~1e-4 (varia 10x fra corse: dipende dagli sprite luminosi transitori); 0 NaN
ovunque.

**Stallo intermittente (fronte 5): 4 corse da 240 s con `GCM_WSTAT=1
GCM_TIMELINE=400000 RSX_FRAMESTAT=50` (stall1-4), tutte a f#3550-3700 senza
fermarsi** (7 su 7 con WSTAT contando c57-c59). Non catturato; o e' raro (1
su 8+) o il WSTAT ne cambia il ritmo. 50 fotogrammi ogni 6,2-6,4 s = 7,9 fps
anche dopo f#3500.

**Worker Gemini: appeso.** Lanciato alle 10:53 sul confronto c62/c63
(`tasks/TASK.md`), dopo 16 minuti nessun messaggio nel database di opencode
(ultimo alle 01:21 di notte) e nessun errore nel log: ucciso, confronto fatto
a mano con un `grep | sort | uniq -c | diff` (30 righe). Il vecchio task
dell'audit degli helper SPU e' in `tasks/TASK_SPU_AUDIT.md`.

**Velocita': dove va davvero il fotogramma (misurato, s.34).** `sample` di
5 s nel livello (`scratchpad/sample.txt`, parser `sample_threads.py`): 6 thread
lane SPU occupati il 45-63% (busy 681-946 su 1510 campioni), profilo PIATTO
dentro il codice liftato (jobmod_008dc900 = job 215, nessun helper caldo:
spu_indirect_branch 152, spu_cflts 60, retlink/tailret/pcwatch/tlv ~200
campioni su ~4.500 di lane); thread PPU principale busy 53% di cui 26% a
girare in `vm_read32` (poll su 0x2000A910: aspetta i job); walker RSX busy
17%; il resto attese. Nessun thread saturo: e' una catena di latenze.
- Timeline di un fotogramma (`GCM_TIMELINE=400000`, f#~2600, 123 ms):
  put+flip-req+waitflip-in a t0; present 8,5 ms; 12 `walk-park` uno ogni
  5-8 ms (i blocchi SPU arrivano cosi') fino a t0+82 ms; waitflip-out
  (timeout 64 ms) a t0+78; `walk` (fine della FIFO) a t0+82; PUT successivo
  a t0+123: **41-46 ms di PPU da solo** fra la fine del flip e il put dopo.
- **Le lane SPU sono il collo nella fase di walk**: `SPURS_FORCE_SPUS=2`
  -> 4,3-4,5 fps, `=3` -> 5,5, 5 (default del titolo per wid 12) -> 7,5-8,
  **`=6` -> 8,2-8,7 fps** (f#2300-2500, tre corse di riferimento 7,7-8,3).
  Modello: fotogramma ~ 80 ms + (224 ms di lane per fotogramma)/N lane. Il
  lavoro SPU e' quasi tutto wid 12 (3,5-3,7 s di lane ogni 2 s; wid 13 0,4 s;
  wid 7 0,26 s su una lane; gli altri trascurabili).
- Salti del trampolino: **50 milioni al secondo** (nuovo contatore in
  `[lane-stat]`: "salti del trampolino: N in 2 s (M per ms di lane)"), 16.000
  per ms di lane = un salto ogni ~62 ns, cioe' ogni ~15 istruzioni SPU.
  `SPU_HOPHIST=215`: il 78% dei salti sta in 8 pc del job 215 (11298 da solo
  il 21%): un ciclo caldo spezzato in piu' funzioni liftate dai bersagli delle
  jump table (`lift_jobmods.py` li aggiunge come `--extra-funcs`), e la
  caduta in avanti fra un pezzo e l'altro passava dal trampolino.
  Fatto: `--fallthrough-tailcall` in `tools/spu_lifter.py` (e passante in
  `lift_jobmods.py`, acceso in `uncharted-data/relift_spu.sh`): la caduta
  nella funzione successiva e' `SPU_TAILCALL` (musttail) invece dell'hop;
  i salti veri restano sul trampolino (interrupt, lockstep, diagnostiche).
  Misura: salti da 16.000 a 11.500 per ms di lane (-28%), fps 7,8/8,0/7,7
  contro 7,6/7,8/7,4 nelle stesse finestre: **entro il rumore**. L'hop non
  era il costo; il costo e' il corpo del codice liftato (registri in
  memoria a ogni blocco, blocchi da ~15 istruzioni).
- PPU liftato a -O2 (`PPU_OPT=-O2 ./build.sh`, 6 minuti con 13 clang++ in
  parallelo; gli oggetti -O1 sono in `obj/o1_backup/`): +0,2-0,3 fps
  (8,2/8,4/8,3/8,0/7,6 contro 8,2/8,1/8,3/7,7/7,2). Tenuto.
- **`--merge-blocks` nel lifter SPU (fatto, acceso per i moduli di job).**
  Prima OGNI bersaglio di salto (anche `brnz` di un ciclo) apriva una
  "funzione" liftata: il job 215 era 240 funzioni = blocchi base, e il C non
  vedeva mai un ciclo. Ora funzioni = entry dell'immagine (LS base, sempre)
  + simboli + bersagli di brsl/brasl + tabelle di puntatori; tutto il resto
  (bersagli di salto, ritorni via link register, jump table, gli
  `--extra-funcs` dentro una funzione, l'indirizzo dopo ogni brsl/bisl) sono
  ETICHETTE: `goto` dentro la funzione, registrate nella tabella con la
  funzione che le contiene, e ogni funzione con etichette ha in testa
  `switch (ctx->pc) { case <etichetta>: goto ... }` per gli ingressi da
  trampolino/`bi`/ritorno a depth 0. Gli archi all'indietro passano da
  `SPU_BACKEDGE`: se c'e' un interrupt in sospeso o il lockstep e' acceso
  vanno dal trampolino come prima, altrimenti `goto`. I brsl impostano
  `ctx->pc` prima della chiamata annidata. Modulo 215: 240 -> 153 funzioni.
  Errore trovato e corretto al primo giro: senza il seme dell'inizio
  immagine il codice fra LS 0x4000 e il primo brsl (modulo 212: fino a
  0x52D0, con l'ingresso 0x4218 raggiunto da `br`) restava senza funzione ->
  `BRANCH-TO-0 unresolved pc=0x04218` e il PM finiva nei propri dati
  (`stopd @0x15B4`), gioco fermo prima del livello. Verifica: la traccia dei
  dispatch di un job (`SPU_JOBTRACE=1 SPU_JOBTRACE_OVL=227 SPU_JOBTRACE_ONE=1`,
  3000 voci) e' IDENTICA fra i due lift. Misura: hop da 11.500 a 8.100 per
  ms di lane (quelli rimasti sono del PM, non merged: 0x2A68/0x2448/0x2C50),
  fps +0,2-0,3 (8,5/8,5/8,5/8,1/7,7 contro 8,2/8,4/8,3/8,0/7,6); ma il tempo
  nei corpi liftati NON cala (`sample`: jobmod 2637 contro 2302 campioni):
  il costo e' per istruzione (gpr in memoria, helper u128), non i confini.
- **Tutto insieme (merge + -O2 + `SPURS_FORCE_SPUS=6`, ora default in
  `run.sh`): 8,6/8,8/8,7/8,8/8,2/8,5/8,6/8,3 fps contro 8,2/8,1/8,3/7,7/7,2/
  7,6/7,8/7,4 = +12%.** Per andare oltre: (a) cache dei registri nel lifter
  (variabili locali per i gpr con scrittura in ctx solo a chiamate/canali/
  trasferimenti: lavoro lungo), (b) helper caldi (shufb/rotqby/float), (c)
  il PM (spurs_policy) e' ancora a blocchi base e fa 25 M hop/s, (d) i 41-46
  ms di PPU da solo per fotogramma e gli 8,5 ms di present.

**Lo stallo intermittente e' la MORTE di Drake (trovato nella corsa di
stabilita' stab6, 240 s, 6 lane + merge).** A 130 s i fotogrammi si fermano
a f#2950 e il log dice `Informing Npcs of player event: 'kPlayerDied'`,
`CameraControlExplosionDeath`, `579_DeathStinger.xvag`: senza tasti (dopo i
28 s di `LBP_AUTOPRESS_UNTIL`) Drake sta fermo e i pirati lo uccidono, in un
tempo che varia da corsa a corsa (nelle 4 corse stall1-4 non e' successo in
240 s). Dopo la morte il gioco continua (NPC, prefetch, e ripete
`RemoveWorkload(wid=12)` + `AddWorkload(wid=12)`: ricrea il workload della
geometria) ma il walker resta parcheggiato per sempre su
`[parkgen] park a 2060D300` = il buffer di comandi per fotogramma a EA
0x20600000 (IO 0x0A500000), FUORI dagli anelli delle lane (0x20800000+),
esattamente come lo 0x2062F660 della s.33: quel park lo patcha un job che
non gira piu'. Ipotesi da verificare (s.35): la nostra HLE di
`cellSpursRemoveWorkload` butta via i job in coda invece di aspettarli (sul
ferro il gioco aspetta che il workload sia scarico prima di rimuoverlo), o
il gioco resetta la FIFO alla morte e il nostro walker non lo vede. Per
riprodurlo: corsa da 240+ s senza tasti e aspettare `kPlayerDied`; oppure
un `PAD_SCRIPT` che lo faccia morire subito. Con lo stallo spiegato, il
fotogramma "slavato" di c62 (effetto danno) e lo stallo sono la stessa
storia: Drake che viene colpito.

### Fronte aperto (dopo la sessione 34)

1. Tonalita' contro RPCS3: serve uno scatto RPCS3 in stato pulito (senza
   l'effetto danno) alla stessa inquadratura; il nostro e' `scratchpad`
   c1-c4 (media 130/121/108). L'icona della pistola nell'HUD manca da noi.
2. Velocita' (ora 8,2-8,8 fps nel livello): le lane SPU restano il collo
   nella fase di walk (fotogramma ~ 80 ms + 224 ms di lane / N lane). Leve
   in ordine: cache dei registri nel lifter SPU; helper caldi; il PM a
   blocchi base (25 M hop/s); i 41-46 ms di PPU da solo e gli 8,5 ms di
   present per fotogramma.
3. Stallo dopo la morte di Drake (v. sopra): il walker resta parcheggiato
   sul buffer per fotogramma 0x20600000 dopo `RemoveWorkload(wid=12)`.
   Verificare la HLE di RemoveWorkload (aspetta i job in coda?) e cosa fa il
   PPU con la FIFO alla morte (`GCM_RECDBG=1`, `GCM_WSTAT=1`).
4. IFE/LOOP nel decompilatore FP; `RSX_DUMP_COUNT>1`; VP/FP per coppia.
5. Virgola mobile SPU: `spu_fi` pass-through; se un job di animazione/fisica
   "esplode", cercare li'.

## Sessione 33 — braccia e gambe di Drake: la SPU non ha i NaN

**La causa: le giunture "twist" della muta arrivano allo skinning come
riempimento `FFFF0000 FFFF0000 FFFF0000 7FFF0000`, e sull'host quello e' NaN.**
Il job 218 (`jobmod_00903d80`, costruzione della tavolozza) scrive per Drake
una tavolozza di 142 matrici 3x4 (PUT da 0x1AA0 a 0x207092D0, base +0x30 per
slot: `SPU_PALETTE_PUT=218`, nuovo) e 16 slot (117 119 121 122 124 125 127 128
130 132 134 135 137 138 140 141 — coppie: le twist di braccia e cosce) escono
con quel riempimento; anche i pirati ne hanno (4 su 122, 8 su 141, 12 su
188) ma non nelle parti che si vedono. Gli ingressi del job sono puliti
(`SPU_NANSCAN=218`, nuovo: 0 quadword con NaN in 5 GET da 0x14D0-0x1F10): e'
il job stesso a lasciare quegli slot al valore sentinella (costanti da
`ilhu`). Il job 215 (skinning) li legge con la lista DMA di ogni parte
(`SPU_LIST_DUMP=lo:hi[,FROM]`, nuovo: tutta la lista quando un elemento tocca
la finestra: 20-30 elementi, matrici singole o contigue a 0x30 di passo) —
con peso 0, che sul ferro basta.

Sul Cell la SPU in singola precisione NON e' IEEE: esponente 255 e' un numero
finito "esteso" (fino a 2^128), non esiste NaN ne' infinito, i denormali
valgono 0, il risultato e' troncato verso lo zero e satura a +-0x7FFFFFFF.
Quindi `0x7FFF0000 * 0 = 0` e la giuntura non pesata non fa danni. I nostri
helper (`spu_helpers.h`) facevano `a * b` in float host: NaN * 0 = NaN, i
vertici delle cosce/braccia diventavano NaN e Metal li scartava (da qui i
"frammenti passati 3.209 su 20.000", il depth del ponte sotto le cosce, la
"striscia" e i "tre gruppi" della s.32: erano i vertici SUPERSTITI, quelli
senza influenze twist). Ora `spu_fa/fs/fm/fma/fms/fnms/fceq/fcgt/fcmeq/fcmgt/
frest/frsqest/cflts/cfltu` passano da `spu__f2d` (esponente 255 -> 2^105 * m,
esponente 0 -> 0, normale -> via veloce) e `spu__d2f` (saturazione,
denormale -> 0, troncamento) in double: il prodotto di due mantisse e' esatto.
Costo: la prima versione (ldexp sempre, nextafterf) dimezzava i fotogrammi nel
livello (f#2288 in 100 s contro 2450); con la via veloce per i normali si
torna a f#2467 in 100 s. `tests/spu_helpers_audit.c` aggiornato alla stessa
semantica (`SPUV`): 1,9 M confronti, 0 differenze. Risultato
(`docs/img_s33_drake_intero.jpg`, c18): Drake intero, muta, braccia, gambe,
piedi; pirati e ponte come prima.

**La pista sbagliata della s.32 (mezza sessione): "il PPU scrive la matrice
16 byte piu' in basso".** Il confronto fra le scritture del PPU
(`func_00474398`, 4x4 da 64 byte) e le GET del job (3x4 da 0x30) a
0x2078A530/0x2078A540 mischiava DUE FASI: il PPU scrive li' solo nel menu/
caricamento (92 scritture, tutte prima del livello: `[WWATCH]` non ha limite
su POSIX ma si ferma perche' il titolo smette), nel livello la stessa memoria
e' la tavolozza del job 218 (0x30 di passo, base diversa per fotogramma). La
regola: ogni confronto fra scrittori e lettori va fatto NELLO STESSO
FOTOGRAMMA (`SPU_DMA_EAWIN_FROM`, `SPU_PALETTE_FROM`, `SPU_LIST_DUMP_FROM`,
tutti con il numero di fotogramma RSX).

Strumenti nuovi, tutti a costo zero se spenti:
- `SPU_MEMDUMP=<ea>:<len>` (con `SPU_DMA_EAWIN`): al primo hit della finestra
  la memoria principale come righe di 4 float — la struttura intorno a una
  GET (passo, righe (0,0,0,1), sentinelle).
- `YDKJ_VWATCH=<lo>:<hi>`: gli `stvx` del PPU (memcpy diretti nel codice
  liftato, invisibili a `YDKJ_WWATCH`) con l'indirizzo dell'istruzione guest
  (il lifter emette la chiamata `ppu_vwatch_hit`; richiede il re-lift, fatto).
- `YDKJ_PAGEWATCH=<ea>[,_FROM=<f>]`: la pagina host va in sola lettura dopo la
  prima write32 del PPU dal fotogramma dato; la PRIMA scrittura successiva da
  QUALUNQUE via (memcpy dei job, HLE, stvx) fa fault e stampa pc, simbolo,
  funzione guest e catena; le write32 del PPU aprono e chiudono da sole.
  Nessuno scriveva 0x2070AD40 nel livello: la pagina restava muta (giusto).
- `SPU_PALETTE_PUT=<ovl>`, `SPU_PALETTE_FROM`, `SPU_NANSCAN=<ovl>`,
  `SPU_LIST_DUMP=lo:hi` + `SPU_LIST_DUMP_FROM`; `SPU_PALETTE_CHECK` ora
  esamina ogni matrice delle GET multiple di 0x30 e stampa il fotogramma.

**La catena di post-processing leggeva i quadranti come memoria vuota
(RISOLTO).** Dopo il pass HDR Uncharted lavora dentro 031E0000 (640x360
RGBA16F) per quadranti: scrive il downsample in alto a destra (scissor
320,0) e lo rilegge come texture "320x180 @ 031E0A00" (= base + 320 px), poi
"@ 032C1000" (= base + 180 righe) per il bloom sfocato che entra nella
composizione finale (A310E8C2: tex0 HDR, tex1 quadrante, tex2 160x90
031B0000, tex3 depth). L'offset non era la base di nessuna superficie: la
ricerca falliva, si decodificava la memoria guest (vuota) e 03120000 ->
03EB/03EC/03ED0000 -> 00030000 (log-luminanza media 1x1) e 03150000/
03180000/031B0000 (bloom) restavano neri. Ora `surface_subview_lookup`
riconosce un offset DENTRO una superficie (stride = larghezza x byte per
texel), tiene una texture della misura chiesta e la riempie con una blit dal
quadrante prima del pass che la campiona (`subview_refresh`, nell'encode
accanto al refresh delle copie del depth). Con `RSX_FRAMEDBG=1` si vede
"texture 320x180 @031E0A00 = superficie 031E0000 + (320,0)". Nota: la
log-luminanza 1x1 (00030000, fmt 14D) non la legge nessuno (ne' PPU, ne'
SPU, ne' RSX, ne' NV0039: `YDKJ_PAGEWATCH=C0030000 YDKJ_PAGEWATCH_READ=1`,
`SPU_DMA_EAWIN`, `GCM_NV0039_TRACE`, `GCM2D_TRACE` tutti muti) — il titolo
usa solo NV0039 fra i metodi 2D. `[drawlist]` ora marca `MAIN` le superfici
con DMA context 0xFEED0001 (`NV4097_SET_CONTEXT_DMA_COLOR_A` decodificato;
nel livello nessuna).

**Confronto con RPCS3 (`docs/img_s31_rpcs3_dive_boat.jpg`).** La nostra
scena e' piu' satura e luminosa (cielo azzurro, mare blu) dove RPCS3 e'
grigia e velata; la scena vera del gioco e' soleggiata, quindi non e' detto
che l'oracolo abbia ragione qui. I "blob bianchi" sono sprite TRANSITORI
(vampe/schizzi: nel fotogramma della sonda c32 non ci sono e la scia della
barca a destra e' corretta); 6852BBAF e' il materiale del mare
(`RSX_FP_PROBE`), 7D51FF19 piccoli sprite sul ponte. Le texture DXT5
256x256 del livello sono normal map (G, A) e si decodificano bene
(`RSX_TEX_SAVE` ora salva anche l'alpha in `_a.pgm`; `RSX_TEX_SAVE_FROM`).
L'icona della pistola nell'HUD non e' disegnata (2 soli draw sul display:
composizione + "40"): probabilmente uno stato dell'HUD, non un difetto.

**Velocita': dove va il tempo del fotogramma (5,5-6 fps nel livello, misurato).**
Strumenti: `sample` di macOS sul processo (4 s), `RSX_FRAMESTAT=50`,
`GCM_WSTAT=1`, `SPURS_LANE_STAT=1` e il nuovo `GCM_TIMELINE=1` (eventi
`[tl] <us> put/walk/walk-park/flip-req/present-in/out/waitflip-in/out`
sullo stesso orologio di `[ring]`). Il quadro:
- Il fotogramma dura ~180 ms. Il walker RSX consuma ~30 blocchi dei job SPU
  per fotogramma, UNO per passata (442 passate in 2 s, 98% parcheggiate: il
  blocco successivo non e' ancora scritto). `GCM_PARK_POLL_US=200` (invece
  dei 4 ms del ticker) non cambia niente, `GCM_RING_GUARD=0` nemmeno: i
  blocchi arrivano davvero uno ogni ~6 ms.
- Le lane SPU stanno nel job 215 il 38% del tempo, in attesa di lavoro il
  45%; `wid=12` (geometria): 6.118 corse del PM in 2 s da <=1 ms l'una, 40%
  di utilizzo delle 5 lane. Un blocco costa ~11 ms di lane (sul ferro ~50
  us): la traduzione SPU e' ~200x piu' lenta del Cell su questo job.
- Il thread PPU: 34% in `cellGcmSetWaitFlip` (SEMPRE per timeout di 64 ms:
  il flip HLE non si completa mai perche' `context->current` al momento di
  cellGcmSetFlip sta DOPO le CALL ai blocchi SPU del fotogramma, e il walker
  ci arriva 180 ms dopo, quando il PPU chiede gia' il flip seguente; il
  timeout e' di fatto cio' che tiene PPU e SPU in pipeline) e ~39% a girare
  in `func_007428C0`/`func_0074073C` da `SwapBuffers` (func_0014F528) e dal
  BigLoop: un poll a vuoto sui risultati dei job (`[HOTREAD] 0x2000A910`).
  Il resto e' logica di gioco.
- La via veloce degli accessi alla memoria guest (`vm_read32` & co. senza
  i ~30 controlli diagnostici e le variabili thread-local, se nessuna
  variabile YDKJ_/FLOW_/LBP_WV/LBP_WW/GCM_SEQ/GCM_JMPWATCH/GCM_PUT_BT/
  RD_FORCE/PPU_WW_GUARD e' impostata; `PS3_VM_FAST=0` la spegne) toglie
  il 60% del tempo CPU del thread PPU ma NON cambia i fps: il PPU aspetta.
- Gli helper float della SPU con la semantica del Cell costavano il 30% dei
  fotogrammi nella prima versione; ora hanno la via veloce (ingressi e
  risultato normali -> float dell'host) e costano come prima.
- Fatto dopo la misura: helper float SPU in NEON (4 corsie, via veloce se
  nessuna corsia ha esponente 255, zero/denormali azzerati; `always_inline`,
  via lenta fuori linea), hook del trampolino (`spu_pcwatch`) con un solo
  flag, contatore di spin di vm_read32 non piu' thread-local: **da 5,5-6 a
  7,2 fps** (RSX_FRAMESTAT=50, tre finestre da 50 fotogrammi: 7,3/7,2/7,2).
  `SPURS_KERN_SLEEP_US=50` vale +0,2, `GCM_PARK_POLL_US=200` 0.
  `GCM_WAITFLIP_MS=24` (il PPU corre avanti prima): 7,5-8,3 fps ma il
  fotogramma e' GUASTO (ombre piatte blu, mare senza riflessi: il PPU
  riscrive risorse che il walker sta ancora leggendo, come in s.28) —
  i 64 ms restano. Il PM rientra ~20 volte per blocco: il job manager
  WwsJob si sincronizza con la lista dei job scritta dal PPU con liste DMA
  stall-and-notify (`[mfc-list] STALL ... loadJobState`), quindi il ritmo
  dei job e' il ritmo con cui il PPU li produce nei suoi ~110 ms di lavoro.
Conclusione: per salire ancora bisogna far girare piu' in fretta i job SPU
(costo per istruzione del codice liftato: helper u128 per valore, salto del
trampolino per blocco con due accessi thread-local, `spu_ls_read128` con
controlli) e ridurre la latenza di dispatch del PM (6.118 corse per ~330
job: ~20 passate a vuoto per job). E' un lavoro lungo, non una correzione.

**CI locale (`.github/workflows/macos.yml`) rimessa in verde.** Il test di
boot POSIX (`tools/test_boot_posix.py`) non linkava piu': l'archivio del
runtime contiene il backend Metal (Objective-C, ARC) e da s.32 il runtime
SPU ne legge il contatore di fotogrammi, quindi servono `-framework
Foundation/AppKit/Metal/QuartzCore -lobjc`. Il controllo `ps3recomp_host
--fp` falliva ("MISMATCH", rosso invece di verde) da s.28: la compilazione
asincrona dei fragment program lasciava al test la coppia incorporata; in
headless ora la compilazione e' sincrona. Ratchet dello scaffold, 63 test
degli helper SPU, 1311 controlli di conformita' PPU, tutti i `tools/test_*`:
verdi.

### Fronte aperto (dopo la sessione 33)

1. Confrontare il fotogramma con RPCS3 (`docs/img_s31_rpcs3_dive_boat.jpg`):
   tonalita' della pelle/muta, riflessi del mare, scie delle barche (blob
   bianchi, sprite 09E6635A con blend ONE/INV_SRC_ALPHA). Nota (c62/c63):
   due corse allo stesso fotogramma 2450, stessa scena (barca dei pirati
   con la scia di 8 ovali bianchi all'orizzonte), una esce slavata (mare
   celeste chiaro, cielo bianco) e una satura: qualcosa di non
   deterministico nella catena HDR/bloom appena riattivata, oppure
   l'esposizione che reagisce agli sprite bianchi sbagliati. Da capire
   PRIMA di giudicare la tonalita' contro RPCS3.
2. Le stesse regole di virgola mobile valgono per i job di animazione/fisica:
   se qualcosa "esplode" (NaN che prima era un numero enorme) cercare li'.
   `spu_fi` resta un pass-through dell'estimate (Newton-Raphson non fatto).
3. IFE/LOOP nel decompilatore FP; `RSX_DUMP_COUNT>1`; VP/FP compilati per
   coppia.
4. Velocita' (7,2-7,9 fps): v. sopra, il collo e' l'emulazione dei job SPU e
   la latenza del dispatch, non il PPU ne' la RSX.
5. Stallo intermittente (1 corsa su 4 da 230 s, c56): dopo il fotogramma
   ~2700 i fotogrammi si fermano (il gioco continua: combattimento, eventi),
   il walker smette di parcheggiare (`[parkgen]` fermo a 0x2062F660, fuori
   dagli anelli delle lane) e la guardia dell'anello scatta la valvola su
   tutte le lane (`prod_v - cons_v` = 1,43 MB = anello meno margine): il
   consumatore e' fermo. Sospetto: il walker bloccato su un ACQUIRE di
   label scritto da un job che la guardia sta frenando (ciclo). Non si e'
   ripresentato in 3 corse con `GCM_WSTAT=1`/`GCM_TIMELINE=400000`
   (c57-c59, f#3500). Da catturare con `GCM_WSTAT=1` ("ultimo ... da N ms").

## Sessione 32 — il fantasma di Drake era la WPOS: i materiali leggevano i buffer di luce con x raddoppiata

**Il fantasma (copia semitrasparente di Drake ~40 px a sinistra, corpo
semitrasparente, sagome fantasma sul ponte) non era un draw in piu' ne'
l'ombra: era la WPOS del fragment program.** I materiali di Uncharted (ponte,
casse, muta, tutti) leggono i buffer di luce in spazio schermo
(tex13 = 016D0000, tex14 = 1E50000, scritti dal pass di luce #1961, e tex12 =
034B0000 normali) con `r1.xy = wpos.xy * c0.xw + c2.xy`, c0 = (1/1280, ...,
-1/720), cioe' aspettandosi una WPOS da 1280x720 con l'origine in BASSO. Il
pass principale e' 2xAA (DIAGONAL_CENTERED_2: superficie fisica 2560x720, i
draw stirati 2x): `input.position.x` di Metal andava da 0 a 2560 → u = x/1280
in [0, 2): con il sampler REPEAT la meta' destra rileggeva la meta' sinistra e
tutto il buffer di luce (con la silhouette di Drake) finiva spostato/ripetuto.
Misurato con la catena di esclusioni: `RSX_SHADOW_FORCE=1` (c01) non toglieva il
fantasma, `GCM_LABEL_UNSTICK_MS=0` (c03) nemmeno, le posizioni dei vertici e le
costanti c0..c3 dei 3 pass del personaggio (034B/007D/016D) erano IDENTICHE
(`RSX_VP_DBG_VP=7BC2B163`), il fantasma stava gia' in 007D0000 alla fine del
pass di colore (`RSX_DRAW_LIMIT_FP=D5601146`, nuovo: dal primo draw con quel FP
in poi non codifica, SOLO nel fotogramma del drawlist), e la sonda
`RSX_FP_PROBE=C1D20673` dipingeva solo la testa (il corpo usa 806F9D7E,
1002B8B8, 4BD7C24D, D7AE5FAD con vp 99180BEE).

Fatto: `NV4097_SET_SHADER_WINDOW` (0x1D88: bit 0-11 altezza, 12-15 origine
1 = basso, 16-19 centro pixel 1 = intero) decodificato in `rsx_state.shader_window`;
nel backend un float4 per draw in `buffer(2)` (`fp_wpos`: sx, sy, bx, by, anello
di 3 buffer) con sx = 1/sc_ax, sy = ±1/sc_ay, by = altezza se origine in basso,
-0.5 con centro intero (RPCS3 `RSXDrawCommands.cpp`: wpos_scale/bias); il
decompilatore FP (MSL, quando `buffered`) definisce `wpos = position.xy * fp_wpos.xy
+ fp_wpos.zw` e la usa per l'ingresso WPOS. Risultato
(`docs/img_s32_wpos_shadowforce.jpg`, con `RSX_SHADOW_FORCE=1`): ponte, casse e
ombre pulite, nessun fantasma, tavolozza di RPCS3.

**RET nel fragment program.** Il decompilatore saltava OGNI op di flow control
(`/* TODO: branch */`): il pass di luce direzionale A3954DDC (= RPCS3
FragmentProgram40: `if (any(lessThanEqual(cc0.xxxx, 0))) return;`) esce subito sui
pixel del cielo, e senza il ritorno campionava la shadow map con coordinate a
caso. Ora RET (opcode SIP 0x45 = dst.opcode 5 con il bit branch) emette l'epilogo
(`fp_emit_return`, fattorizzato) predicato come KIL. Nel livello RPCS3 usa solo
RET e KIL (8 e 42 programmi su 169), niente IFE/LOOP: restano da fare. La sonda
`RSX_FP_PROBE` ora sostituisce l'ULTIMO `float4 _o =` (prima il primo, che con
un RET e' il ritorno anticipato).

**Cosa resta sul personaggio.** Con la WPOS giusta mancano braccia e gambe
della muta (torso, testa, mani, fondina, stivali ci sono). I draw ci sono
(1002B8B8 = gambe/maniche), le posizioni e le costanti sono identiche fra
prepass (5A5B70A5) e pass di colore (99180BEE), la libreria e' compilata
(`RSX_FP_TRACE=<hash>`, nuovo: ricerche in cache, creazione della pipeline,
e nel fotogramma del dump i FRAMMENTI PASSATI per draw dai contatori di
visibilita'), la pipeline si crea una volta sola, `RSX_CULL=0`,
`RSX_EQUAL_LEQUAL=256`, `RSX_DEPTH_FUNC=7` e `RSX_FP_ALIAS=1002B8B8:806F9D7E`
(nuovo: usa la libreria di B per i draw con A) non cambiano nulla, ma i
contatori dicono che i frammenti passano SOLO in parte (3.209 su ~20.000 per
un draw da 492 vertici) e nel buffer LDR 007D0000 prima del resolve la sonda
magenta dipinge solo chiazze (spalle, polsi, stinchi). Il depth del prepass
(`/tmp/rsx_zeta_<off>.ppm` ora con i 24 bit in R,G,B) sotto le cosce ha lo z del
ponte, non delle gambe: il prepass delle gambe non scrive dove dovrebbe.
Sospetto principale: i vertici spazzatura.

**I vertici spazzatura NON vengono (solo) dallo sblocco dei label.** Con
`RSX_VP_DBG_STAT=2` (ora stampa anche l'intervallo di vertici spazzatura) su
3000 draw dal fotogramma 2450: 266-361 draw con posizioni spazzatura con lo
sblocco acceso, 0 con `GCM_LABEL_UNSTICK_MS=0` (c40) — ma in TUTTE le corse gli
sblocchi sono del tipo "altro" (label == r12, cioe' il job NON sta aspettando:
`func_4B48` aspetta solo se r12 < label <= r5, o nel wrap se label <= r23; il
forzare a r12 e' un no-op). Quindi la correlazione con lo sblocco e' da
rimisurare (una corsa per modo non basta), e al present la spazzatura AUMENTA
(l'anello viene riscritto): il walker legge i blocchi mentre il job li sta
ancora scrivendo o li ha gia' riscritti. Aggiunta la soglia
`GCM_LABEL_UNSTICK_READS` (default 8 letture oltre ai 20 ms: 2-3 letture in
30-50 ms sono controlli di blocchi diversi, non un'attesa). Con
`GCM_LABEL_UNSTICK_MS=0` 2 corse su 3 non arrivano al livello in 95 s.

**Altro, misurato.** `sh=` nel drawlist e' `shadow_key`, non SHADER_CONTROL
(mezz'ora persa: i valori di 0x1D60 sono sani, `RSX_SC_TRACE=1` li stampa).
Le compilazioni dei FP: solo ~190 coppie (FP, VP) in 90 s, e con la cache di
sistema di Metal costano 0 ms (le chiavi includono il VP: ogni coppia
ricompila anche il VP; da separare, ma non e' il collo di bottiglia).
`RSX_DUMP_COUNT>1` resta rotto. Gemini a quota esaurita (limite 20 richieste
al giorno): l'audit degli helper SPU (`tasks/TASK.md`) non e' partito.

**I vertici spazzatura: il job lappava il walker (RISOLTO con la guardia
dell'anello).** Con `GCM_RING_TRACE=1` (nuovo: ogni controllo del label da
parte del job con il blocco (r12, r5] e ogni RELEASE del walker) si vede il
protocollo: ogni lane alloca blocchi contigui (vertici 8-16 KB + pacchetto
0x100-0x300) da un anello di 0x199980 byte (lane 0 = 0x20800000, poi
+0x199980 ciascuna, r26 = fine), e prima di scrivere controlla il label:
aspetta SOLO se r12 < label <= r5 (nel wrap, r5 > r26: se label <= r23). La
RELEASE del walker vale la fine del blocco = r12 del successivo, quindi cade
quasi sempre sul confine escluso. Sul ferro la RSX e' piu' veloce degli SPU
e il label sta a ridosso del produttore: basta. Da noi il job fa un giro
INTERO dell'anello in ~700 righe di log mentre il label si sposta di pochi
blocchi: passa il walker e riscrive tutti i blocchi non ancora consumati
(spazzatura 187-435 draw su 3000, solo nel prepass che e' il primo
consumatore). Le RELEASE sono in ordine (11909 crescenti, 85 decrescenti = 65
wrap + 20 reset all'inizio dell'anello). Non era lo sblocco dei label (tutti
"altro" = no-op; A/B ripetuto: on 187/337/331/287/435, off 0/0/0, con lo
sblocco spento 1 corsa su 6 non arriva al livello).

Fatto: `rsx_ring_guard_*` in `rsx_commands.c`: posizione virtuale (giri x
anello + offset) del produttore (dai controlli del job, `spu_dma.h`) e del
consumatore (dalle RELEASE, giro = quello del blocco prodotto con quella fine;
nel wrap la fine e' r23 nel giro dopo); se il blocco nuovo supererebbe il
consumatore di un giro meno `GCM_RING_GUARD_MARGIN` (0x40000), prima della GET
si scrive nel label un valore dentro l'intervallo (r5, o r23 nel wrap) e il job
aspetta da solo; altrimenti il label vero. Valvola `GCM_RING_GUARD_MAX_MS`
(2000) contro i blocchi circolari (mai scattata). `GCM_RING_GUARD=0` spegne.
Misura: 0 draw spazzatura su 3000 in 3 corse su 3, livello raggiunto, stesso
numero di fotogrammi in 95 s (f#2452-2453 contro f#2178-2450).

**Ma braccia e gambe mancano anche con i dati puliti: le mesh delle gambe
sono SCHIACCIATE.** `RSX_VP_DBG_VERTS=2000` (nuovo) stampa tutti i vertici:
il draw della coscia (prepass 5A5B70A5, 465 vertici, 123 posizioni distinte,
123/155 triangoli consecutivi condividono un lato: e' una lista di triangoli
sana) ha il bbox giusto (24 x 77 x 26 cm) ma area totale 0.045 m² (una coscia
ne fa ~0.2) e 977 px² su schermo per una striscia 41 x 200: i vertici stanno
su una superficie sottile — lo skinning multi-osso (cosce, stinchi, braccia)
esce collassato verso l'osso; testa, torso, mani (1 osso) no. I contatori di
visibilita' confermano: 2.042-3.569 frammenti per draw invece di ~20.000. E'
un errore di calcolo SPU nel job di skinning (o nelle matrici dei joint): il
precedente e' `fi`. Serve l'audit degli helper (`tasks/TASK.md`).

**Le ombre: la RSX confronta TEXEL OP RIFERIMENTO.** Con la WPOS giusta la
scena era quasi tutta in ombra. Il decompilatore emetteva `c.z OP z` (ref OP
texel, la convenzione GL/Vulkan); RPCS3 (`VKDraw.cpp`:
`get_compare_func(tex.zfunc(), true)`) inverte la direzione per le texture
con zfunc: GEQUAL diventa "texel >= ref", cioe' il test d'ombra classico.
Ora il default e' `z OP c.z` (`RSX_SHADOW_FORCE=5` = l'ordine GL per l'A/B).
Risultato (`docs/img_s32_shadows.jpg`): ponte, casse e ombre come in RPCS3
(ombra delle casse a destra, di Drake ai piedi a sinistra), senza forzature.

`SPU_PROBE_PC`/`SPU_PROBE_REGS`/`SPU_HOPHIST` (nuovi, `spu_pcwatch`): i kernel
caldi del job 215 nel livello sono a 0x11260-0x13510 (0x12A98/0x12D68...),
NON 0xD5AC/0xD070; solo i blocchi raggiunti dal trampolino si vedono.

**Le gambe: cosa e' stato escluso (s.32, tanto lavoro, nessuna causa).**
- Le mesh delle gambe/braccia della muta escono a pezzi: nel draw della
  "coscia" (465 vertici) ci sono TRE gruppi (`RSX_VP_DBG_VERTS=2000` +
  proiezione in Python): il polpaccio in basso e' un tubo ben formato, in mezzo
  una pezza ripiegata, in alto un frammento. I pirati (gambe nude, pantaloncini)
  sono interi: il difetto e' della risorsa/rig di Drake.
- Non sono gli helper SPU: `tests/spu_helpers_audit.c` confronta 110 helper
  con un'implementazione dalla specifica CBEA su 1,9 milioni di ingressi
  casuali: 0 differenze (shufb, rotq*/shlq*, cwd/chd/cbd/cdd, fsm*, mpy*,
  conversioni, confronti, gb*, sumb...).
- Non e' il lifter SPU: `SPU_INTERP_OVL=215,2440` (nuovo) fa girare tutto il
  job 215 nell'interprete dal fotogramma 2440: stesse gambe mancanti.
- Non e' il PPU: `lvewx` & co. ora caricano il quadword intero (prima
  l'elemento a byte 0, 8.180 siti Vectormath; RPCS3 fa lo stesso) e le 16
  istruzioni VMX che erano no-op (vsl/vsr/vslo/vsro/vpkuhum/vpkuwum/vrlh/vsrb...
  141 siti) sono implementate: identico. Le istruzioni PPU non tradotte sono
  finite (restano `.word` = dati e 35 `vmx_x1604` = parola 0x11417E44 non
  standard, probabilmente dati).
- Le matrici dei joint: il job le prende UNA per volta (GET da 0x30, 8.700 per
  corsa) dall'area 0x20700000-0x20800000 (`SPU_PALETTE_CHECK=215
  SPU_PALETTE_EA=lo:hi`, nuovo): il 22% non e' ortonormale ma sono oggetti
  scalati (0.1) e strutture non-matrice; le "NaN" sono riempimenti
  0xFFFF0000/0x7FFF0000. Niente di conclusivo.
- Restano: i dati della risorsa (formato dei vertici della muta: pesi/indici a
  4 ossa, ossa "twist", remap degli indici) o la catena di caricamento
  (decompressione). Oracolo possibile: la cattura RSX di RPCS3 (i vertici che
  arrivano alla RSX per lo stesso draw).

### Fronte aperto (dopo la sessione 32)

1. Braccia e gambe della muta schiacciate (skinning multi-osso): audit degli
   helper SPU contro RPCS3 (Gemini a quota esaurita: farlo a mano o con il
   worker locale), partendo da shufb/rotq*/selb/cwd e dalle conversioni.
2. (risolto: guardia dell'anello) — tenere d'occhio `[ring-guard] valvola`.
3. (risolto: ordine del confronto d'ombra; sampler per unita' fatti)
4. Le scie delle barche sono blob bianchi (sprite 09E6635A con blend
   ONE/INV_SRC_ALPHA).
5. RSX_DUMP_COUNT>1, audit helper SPU (Gemini), separare VP/FP nelle librerie.

## Sessione 31 — l'oracolo RPCS3 gira sul Mac; cubemap e probe RGBA16F: il personaggio ha il suo materiale

**RPCS3 esegue Uncharted su questa macchina** (`uncharted-data/tools/RPCS3.app`,
build arm64 0.0.42, Vulkan/MoltenVK): titolo, menu, livello dive-boat a
~17 fps (`docs/img_s31_rpcs3_dive_boat.jpg` = il riferimento del fotogramma
che stiamo inseguendo). Ricetta, perche' ognuno di questi passi e' costato:

- Firmware: quello del disco (`/tmp/uc_iso/PS3_UPDATE/PS3UPDAT.PUP`, v1.9x)
  installato con `rpcs3 --installfw <pup>`. E' VECCHIO: manca
  `libmpeg2dec.sprx`, e RPCS3 moderno decodifica i filmati chiamando i moduli
  del firmware -> `cellVdecQueryAttr` FATAL, il titolo resta per sempre in
  "waiting for pic" sul filmato dei loghi. Rimedio senza scaricare un PUP nuovo:
  `uncharted-data/rpcs3_bdvd/` e' un'ombra del disco fatta di symlink SENZA
  `sce-ndi-logos.m2v` e `00a-01a-sequence.m2v` (il titolo salta i filmati
  mancanti senza lamentarsi). Si avvia con
  `rpcs3 --no-gui ~/Documents/Coding/uncharted-data/rpcs3_bdvd/PS3_GAME/USRDIR/EBOOT.BIN`.
- Gli overlay di RPCS3 vogliono un font: senza `~/Library/Fonts/Arial.ttf`
  (symlink a `/System/Library/Fonts/Supplemental/Arial.ttf`) il Pad Thread
  muore con "Failed to initialize font" e il gioco non parte.
- Tastiera: `~/Library/Application Support/rpcs3/input_configs/global/Default.yml`
  con **Cross = Backspace**, Start = Return, Circle = Del, Triangle = Esc. Il
  motivo: i tasti si mandano dal computer-use MCP in background (`app_key`)
  che conosce solo return/escape/backspace/delete; `app_type "x"` arriva come
  testo, non come tasto, e RPCS3 non lo vede. Una pressione singola spesso
  cade fra due poll del titolo: 8 backspace di fila in un `app_batch`.
  Sequenza: autosave -> Cross; "Press START" -> Return; New Game -> Cross;
  difficolta' -> Cross; "save this data?" -> Cross; ~60 s dopo il livello.
- `Log shader programs: true` in `config.yml`: RPCS3 scrive il GLSL di ogni
  programma in `~/Library/Caches/rpcs3/shaderlog/` (188 file per il livello,
  `FragmentProgramN.spirv` e' testo GLSL). NON c'e' il microcodice ne' un hash:
  si appaiano con i nostri per struttura (unita' texture, TEX2D_SHADOW,
  numero di istruzioni). TTY del titolo in `~/Library/Caches/rpcs3/TTY.log`.
- Non lanciare il port mentre RPCS3 gira (stessa GPU).

**Cosa ha detto l'oracolo sul personaggio** (FP F728899C = muta, 5F6F3ABD =
testa, in RPCS3 `FragmentProgram129/130.spirv`): `tex2` e' un **samplerCube**
campionato con la normale (ambiente/probe), `tex15` un `sampler1D` (LUT),
`tex12` `sampler2DShadow`. Il backend Metal passava sempre `tex_cube_mask=0`
al decompilatore e caricava ogni texture come 2D: il termine ambiente del
personaggio leggeva un colore qualsiasi della prima faccia. E la probe e'
una cubemap **16x16 W16_Z16_Y16_X16_FLOAT (0xBA)**, formato che
`rsx_texture_layout` non conosceva (letta come B8: rumore).

Fatto (`rsx_metal_backend.m`): (1) `cube_mask` per draw dal bit 2 di
SET_TEXTURE_FORMAT, dentro la chiave della variante del FP (`shadow_key`) e
passato al decompilatore (che aveva gia' il percorso `texturecube`);
(2) upload a 6 facce in una `MTLTextureTypeCube` — ogni faccia e' la sua
piramide di mip completa allineata a 128 byte (come nel backend D3D12), le
viste con remap prendono 6 slice; (3) decodifica di 0x9A in una texture
`RGBA16Float` (parole a 16 bit BE, ordine gia' RGBA dopo lo scambio, RPCS3
`TextureUtils.cpp`). Risultato: la muta e' grigio-blu con il riflesso del
cielo e il disegno circolare della bombola sulla schiena, il container verde
acido e' diventato grigio-azzurro: la tavolozza e' quella di RPCS3.
`RSX_CUBE=0` per l'A/B, `RSX_CUBE_DBG=1` stampa cubemap e varianti.

**Il personaggio ERA a pezzi, e non era lo skinning: era `fi`.** (Correzione
della nota di s.30: in RPCS3 Drake sta in piedi con le braccia lungo i
fianchi; da noi la mano fluttuava sopra la testa, il busto era deformato e
delle gambe restavano frammenti.) `spu_fi(a, b)` faceva il passo di Newton
del reciproco `b*(2-a*b)`: giusto dopo `frest`, SBAGLIATO dopo `frsqest`
(b = 1/sqrt(a): con a=4 dava 0, con a=0.25 dava 3). Sull'hardware `fi`
completa l'una o l'altra stima leggendo il passo codificato nei bit bassi
della stima; le nostre stime sono gia' esatte, quindi `fi` e' l'identita'.
Ogni normalizzazione di vettori/quaternioni del codice SPU (skinning,
normali, camera) passa da `frsqest+fi` (es. job 215 a 0x...: `frsqest; fi;
fm; fnms; fma` = sqrt con un passo di Newton). Risultato
(`docs/img_s31_drake_fi.jpg`): Drake intero, in piedi, muta, gambe e piedi
nudi, e ANCHE la camera e' ora quella di RPCS3 (dietro di lui, vicina).
Insieme: `frsqest` in valore assoluto e senza inf (0 -> float massimo, come
la saturazione dell'hardware), `frest(0)` idem; e due UB trovati con UBSan
sui moduli liftati (`SPU_OPT="-O1 -fsanitize=undefined" SPU_LDFLAGS=
-fsanitize=undefined RT_BUILD=build-ubsan ./build.sh`): NaN -> int in
`cflts/cfltu` (ora 0) e `<< 16` di un prodotto negativo in `mpyh` (ora
senza segno). Resta sul personaggio una semi-trasparenza (gambe e pistola si
vedono attraverso).

**Trappola costata due ore: `RSX_DUMP_COUNT>1` rompe i fotogrammi dopo il
primo.** Con `RSX_DUMP_COUNT=4` il fotogramma 1 e' giusto, 2 e 3 escono
PIATTI (colore uniforme 0x363A4E + HUD) e il 4 esce con le sagome piatte e il
ponte mancante: NON e' il gioco, e' il percorso del dump (readback/stato dopo
il primo salvataggio). L'avevo preso per una regressione del rebuild dei
moduli SPU (ho cancellato `obj/spu_*.o` e ricompilato: identico, era il
dump). Regola: per un confronto UN solo dump per corsa (`RSX_DUMP_COUNT=1`,
o `RSX_DRAWLIST_AFTER=<n> RSX_DUMP_FRAME=100000`). Da sistemare il dump, o
misurare "meta' fotogrammi piatti" con uno screenshot della finestra.
`build.sh`: con `set -e`, un `.o` mancante faceva uscire lo script in
silenzio nel controllo degli header (`hdr_new`): corretto; `RT_BUILD=<dir>`
sceglie la libreria runtime, `SPU_LDFLAGS` va al link.

**Le pipeline Metal erano condivise fra programmi diversi** (`pso_key`,
sera): la chiave era una parola a 32 bit con i campi impacchettati e nel
ramo con blend il fragment program contribuiva con UN bit: ogni draw con
blend e lo stesso stato di blend (stesso VP) prendeva la pipeline del primo
programma capitato, e le varianti del FP (ombre, cubemap) condividevano la
pipeline della prima variante. Trovato cosi': la sonda `RSX_FP_PROBE=
43C6091F:float4(1,0,1,1)` dipingeva le gambe di magenta nel passaggio EQUAL
(034B0000) mentre col programma vero le gambe NON venivano scritte (la sonda
cambia la pipeline). Ora la chiave e' un hash a 64 bit di tutto (programma,
variante `shadow_key`+`cube_mask`, blend, formato, MRT, maschera, export
z), e la coppia incorporata (FP non ancora compilato) sta in una voce sua
(vp = -2) invece di rubare quella dei draw con VP non compilabile. Effetto:
il buffer delle normali ha Drake INTERO (gambe e braccia), il ponte e' del
colore caldo di RPCS3 (era verdastro), le casse giuste. Resta (in
016D0000, prima del composito): una copia scura semitrasparente di Drake
spostata a sinistra di ~40 px e il corpo semitrasparente. Non e' nessuno dei
draw con blend del pass finale (sondati 09E6635A = sprite, 6852BBAF = mare,
67C98421, 6EBA84BF, C9A90509, 7D51FF19, 3CA5C8B6: nessuno la disegna) e non
e' il composito (`RSX_FP_PROBE=A310E8C2:float4(rsx_tex[0].sample(...` mostra
016D0000 tal quale): resta il pass di luce direzionale con ombra
(#1961: FP A3954DDC, MRT x3, legge 2D50000 depth, 3EE0000 shadow, 034B0000
normali). Ipotesi: e' l'OMBRA di Drake proiettata nel posto sbagliato (in
RPCS3 sta ai suoi piedi a sinistra). `RSX_SHADOW_FORCE=1` non ancora
misurato (due corse senza fotogramma sopra soglia).

**Dati dei vertici letti a meta' scrittura** (`RSX_VP_DBG_STAT=2` +
`RSX_VA_RECHECK=1`): in ~1 corsa su 2 i blocchi di posizione del prepass
(job 215, PUT da 16 KB a 0x20800000+ da LS 0x2F000, lane diverse da quella
che patcha il pacchetto) sono meta' spazzatura al momento della copia e
completi al present. Con `GCM_LABEL_UNSTICK_MS=0` spazzatura 0/940 (c40) —
lo sblocco dei label a 20 ms fa riscrivere al job un blocco che il walker
non ha ancora copiato — ma con lo sblocco spento 2 corse su 4 (c52, c54)
sono rimaste indietro (meno eventi, nessun fotogramma sopra soglia in
125 s): e' il motivo per cui esiste. Da fare: capire l'attesa del job
(label 0x480+16*lane) invece di forzarla. Aggiunto anche il conteggio delle
"generazioni di park" dei pacchetti CALLati (`cellGcm_note_park_put`,
`[stale-call]`, `GCM_STALE_WAIT=0`): il walker NON consuma mai pacchetti
vecchi (0 casi), quindi il PPU/job 217 riparcheggia sempre in tempo.

### Fronte aperto (dopo la sessione 31)

1. Copia-ombra di Drake spostata a sinistra e corpo semitrasparente in
   016D0000 (v. sopra): misurare `RSX_SHADOW_FORCE=1`, poi le coordinate
   della shadow map (atlante 2432x1216, due cascate) nel FP A3954DDC.
1b. Lo sblocco dei label (20 ms) corrompe i vertici del prepass; a 0 il
   gioco a volte si ferma.
2. Il dump multi-fotogramma (`RSX_DUMP_COUNT>1`) corrompe i fotogrammi
   successivi; verificare se anche a schermo alterna fotogrammi piatti.
3. RPCS3 dice anche: `TEX3D` in 62 punti (volumi, unit 1/2) e `TEX1D(15)` —
   da noi tutto 2D. Da verificare cosa si perde.
4. Audit degli helper SPU contro RPCS3 (`tasks/TASK.md`, Gemini era a quota
   esaurita): `fi` e' il precedente per cui vale la pena.
5. Stallo del report (s.30), velocita' 8 fps.

## Sessione 30 — il livello come nel gioco: draw coalescenti, maschera di colore, crash del present

Prima cosa, fuori dal codice: dopo l'aggiornamento a Xcode 27 la licenza
non era accettata (`clang` e `git` rifiutavano di partire: `sudo xcodebuild
-license accept`) e il mount della ISO in `/tmp/uc_iso` era sparito col
riavvio (0 draw dal fotogramma ~200, `Error loading script file
'startup.bin'`, `ASSERTION: val` in `LookupSymbolValuePointer`): `hdiutil
attach -readonly -nobrowse -noverify -mountpoint /tmp/uc_iso "<iso>"`.
Poi la sessione 29 (14/09) e' stata documentata e committata (il worker
Gemini ha estratto i numeri delle 25 corse ed e' caduto per quota).

**1. `MTL_MAX_VERTS` 1M -> 4M** (`MTL_VP_FLOATS` 48M -> 128M float, memoria
virtuale toccata solo se usata): `dropped` da 200-1.500 draw per fotogramma
a **0** in tutte le corse (c2..c10).

**2. Due fragment program del livello (0x0013C002, 0x00146002) uscivano
`decompile failed`**: il tetto di lettura del microcodice era 4096 byte
(`MTL_FP_UCODE_MAX`, ora 16 KB; MSL da 32 a 128 KB). I loro draw finivano
nella coppia incorporata con le posizioni grezze. 0 fallimenti dopo.

**3. SIGSEGV nel present (`objc_retain` dentro `setFragmentTexture`), c3 e
2 corse su 25 della s.29.** Il crash report (`~/Library/Logs/
DiagnosticReports/uncharted-*.ips`) dava la catena: un draw registrava una
`id<MTLTexture>` senza ritenerla (`d->tex` e' `__unsafe_unretained`) e la
codifica la trovava gia' liberata. Due strade: (a) la tabella delle viste
con remap (`s_remaps`, 256 voci, ricerca lineare): a tabella piena la vista
nuova non era tenuta da nessuno -- nel livello le coppie (texture, remap)
superano 256; (b) `texcache_sweep`/`texcache_add` toglievano dalla cache
texture ancora dentro una unita' (il percorso "already resident" non
aggiorna `last_use`), e al rebind successivo l'unita' le liberava con i
draw del fotogramma che le puntavano. Ora: viste in tabella hash da 4096
(`remap_slot`, `remap_rebuild`), a tabella piena tutte nel cimitero e si
ricomincia; le texture escono dalla cache solo se non sono in una unita'
(`tex_held_by_unit`) e passano dal cimitero (`graveyard_add`), vive fino a
fine present; `remap_drop_base` toglie le viste di una base che esce.
Misura: 0 crash in 8 corse (c4..c10, 150-240 s).

**4. `RSX_DUMP_FRAME` fermava il livello** (nota della s.28): il percorso
del dump usciva dal present PRIMA di pubblicare i report delle occlusion
query, e il job 215 aspettava per sempre. Ora `reports_publish()` e'
condivisa fra il completed handler e il dump (la GPU ha gia' finito:
`waitUntilCompleted`). c4: due dump a 240 s senza fermate.

**5. I "triangoli neri" nel cielo: le parole DRAW_ARRAYS spezzavano le
primitive.** `RSX_FP_PROBE=all` sul dump di 007D0000: la zona dei
triangoli era il colore di clear (nessun draw) e il resto del cielo un
solo programma (fp 0019A002/9B3CBE55, 23 draw da **256** vertici, liste di
triangoli). Ogni parola `DRAW_ARRAYS`/`DRAW_INDEX_ARRAY` porta al massimo
256 vertici ((count-1) in 8 bit) e un draw grande arriva come parole
contigue nello stesso BEGIN/END; il dispatcher le mandava al backend una
per una e l'assemblaggio ripartiva a ogni parola: 256 = 85*3+1, dalla
seconda parola i triangoli erano sfalsati di un vertice (sul cielo, una
griglia regolare, diventavano triangoli "plausibili" con i buchi; sulla
gru e le sartie, spuntoni). Ora `draw_clause_append/flush` in
`rsx_commands.c` (come RPCS3 `draw_clause::append`): le parole contigue
diventano un draw, emesso a END. `RSX_DRAW_COALESCE=0` torna a prima.
Risultato: cielo intero, gru e sartie giuste, mare; draw per fotogramma da
~6.000 a ~2.900 (`docs/img_s30_dive_boat_coalesce.png`), fps invariati
(7,5-8).

**6. La sfera e la cupola nere davanti al personaggio: la maschera di
colore.** Nel drawlist erano 2 draw da 540 vertici nel buffer illuminato
016D0000 con un FP banale (34F0B9F8, lo stesso delle shadow map), senza
blend: i volumi delle luci dell'illuminazione differita, che il titolo
disegna con `SET_COLOR_MASK` = 0 (solo stencil/depth). Il backend Metal
non applicava la maschera: ora `d->color_mask` -> `writeMask` della
pipeline (parte della chiave PSO; `cm=` nel drawlist). Nel fotogramma del
livello **1563 draw su 2920 hanno maschera 0** e 1094 maschera RGB senza
alpha. `docs/img_s30_dive_boat_colormask.png`: la scena e' quella del
gioco.

**7. Il personaggio: tre cose in fila, e un fronte preciso (pomeriggio).**
Il personaggio (Drake) usciva nero e "a pezzi". Misure e fix, in ordine:
- Le parti "mancanti" (gambe) e i blocchi piatti grigio-blu sul ponte erano
  l'EQUAL del passaggio di colore che fallisce contro lo z del prepass su
  chunk interi (i due VP calcolano la posizione con arrotondamenti diversi;
  la RSX confronta z a 24 bit fissi e non se ne accorge, il nostro
  Depth32Float si'). Ora `RSX_EQUAL_LEQUAL` e' acceso di default (LEQUAL +
  4 ulp di bias, `=0` per spegnere): ponte intero, gambe al loro posto.
- Il personaggio e' forward-lit nel pass 016D0000 (fp 5F6F3ABD, 27 draw
  dopo il quad di composizione) e il suo FP legge la shadow map (unit 12 =
  03EE0000, 2432x1216, formato depth 0xB2 con zfunc GEQUAL nel bit 28-31 di
  TEXTURE_ADDRESS): tre cose non c'erano: (a) la lettura "shadow" (confronto
  invece del texel: ora `rsx_shadowN()` nel FP, confronto a mano sul texel
  piu' vicino della copia A8R8G8B8 del depth, `rsx_fp_set_shadow`, la
  maschera nella chiave della cache dei FP: `sh=` nel drawlist); (b)
  l'ingresso FACING (input 0xE, SSA) del FP, che il titolo usa come
  predicato per applicare l'ombra e per la luce a due facce (`input_facing`
  da `[[front_facing]]`); (c) le shadow map erano VUOTE: le due cascate sono
  viewport 1216x1216 nelle due meta' della superficie e il backend mappava
  l'NDC su tutta la superficie (caster fuori dal ritaglio) -- ora la viewport
  del guest e' per draw (`draw_viewport_setup`, `RSX_VIEWPORT=0`); e i pass
  solo-profondita' (colore e zeta allo stesso offset) ricostruivano lo zeta
  dal colore fantasma al pass dopo (`synced_seq`).
- Culling: non era applicato (`SET_CULL_FACE`, `SET_FRONT_FACE`): ora per
  draw (`cull=` nel drawlist; il pass 031E0000 usa FRONT, gli altri BACK;
  `RSX_CULL=0`). Con culling + ombre + facing il personaggio riceve luce
  (`docs/img_s30_cull.png`).
- APERTO: il termine d'ombra e' ancora quasi sempre 0 sui ricevitori:
  `RSX_SHADOW_FORCE=4` (mostra il riferimento c.z) da' ~0 su ponte e
  personaggio, `=3` (il texel) da' valori sensati. Il riferimento e'
  `r2.x*c36.x + c36.y` con `r2.x = dot(tc4.xyz|c24.x, c31)` e tc4 = o[11]
  del VP (posizione in spazio luce): o le costanti inline c24/c31/c36 sono
  numerate male, o o[11] esce sbagliato. Con `RSX_SHADOW_FORCE=1` la giacca
  e' bianca (troppa luce), con `=0` la scena e' tutta normale ma il
  personaggio scuro. Il blocco piatto sotto il personaggio in
  `img_s30_cull.png` e' la sua ombra sul ponte (il termine 0 li' e' giusto,
  ma la resa piatta no: con FORCE=0 il ponte in ombra ha la texture).
- Ultime misure (sera): il pass forward disegna SOLO la testa (27 chunk di
  vp 7BC2B163 = pelle/capelli); la giacca e le gambe sono differite (color
  pass 806F9D7E/1002B8B8 + composizione) e ANCHE loro leggono la shadow map
  (con `RSX_SHADOW_FORCE=1` la giacca esce bianca, con la lettura vera resta
  scura). Le costanti inline del FP 5F6F3ABD (`RSX_DRAWLIST_FPC=5F6F3ABD`,
  53 costanti) sono plausibili: c24=(1,0,0,0), c31=(0.0021,-0.0033,-0.0028,
  0.622), c36=(0.9994,0,0.889,0.671); tc4 = o[11] del VP = posizione MONDO
  (dot con c4..c6) con w = clip w. Il riferimento e' quindi
  `0.9994 * (pos_mondo . c31.xyz + 0.622)`, cioe' una profondita' in spazio
  luce LEGGERMENTE minore dello z del caster stesso: con la semantica GL
  (ref >= texel, RPCS3 la usa cosi') un ricevitore illuminato che e' anche
  caster fallisce sempre. O la shadow map del titolo e' "invertita" (piu'
  grande = piu' vicino) e il nostro pass la scrive al contrario, o il
  confronto va fatto sul texel a 16 bit. Serve l'oracolo: RPCS3 con
  RenderDoc sullo stesso fotogramma (shadow map 03EE0000 e zfunc).
- Correzione: 031E0000 (640x360 FP16) NON e' un buffer di luce a mezza
  risoluzione: e' un atlante di piccoli render (una cassa, un pezzo di
  cielo, un blocco verde: probe/riflessi). La giacca e' disegnata nel pass
  finale 016D0000 dal FP F728899C (vp 99180BEE) -- una sonda su di esso
  (`RSX_FP_PROBE=F728899C:float4(h[3].xyz*0.5+0.5,1)`) si vede sulla giacca.
  Quel FP applica l'ombra SOLO se `cc0.z = saturate(dot(h3, -c12)) != 0`
  (N.L verso la luce) e con `RSX_SHADOW_FORCE=3/4` la giacca resta scura:
  la scurezza della giacca non dipende dal termine d'ombra ma da N.L o dal
  resto dell'illuminazione (normale h3 = normal map tex1 .yw + tangenti da
  tc). Prossimo: sonda `float4(saturate(dot(h[3].xyz,-fp_constants[12].xyz)).xxx,1)`
  su F728899C, poi la normale e la luce (c12) contro RPCS3.
- Misure fatte e scartate: `RSX_DEPTH_CLAMP` (nessun effetto), il
  ricontrollo dei buchi di zeri nelle CALL (`GCM_ZERO_TRACE=50`: 919 su 1,3
  milioni scritti dopo il passaggio del walker, tutti parole 00020000 /
  40680100 di riempimento, non draw persi), occlusion off (uguale).

**Stallo intermittente visto una volta (c2)**: `[spu-dma] ovl=215
pc=0x086F4: 200000 identical GET ... ea=0x210157F0 (valore 0)` poi
`Rendering is stuck on reference value 2144`: il job di geometria
aspettava un report (slot 383) mai scritto dal walker (parola 0 = 0,
nessun timestamp). 1 corsa su 9. Da capire; un rimedio nello stile di
`GCM_LABEL_UNSTICK` (pubblicare l'ombra del report dopo N ms) e' l'idea.

### Fronte aperto (dopo la sessione 30)

1. **Il termine d'ombra e' ~0 sui ricevitori** (v. punto 7): partire dal
   riferimento `c.z` (RSX_SHADOW_FORCE=4) -- costanti inline c24/c31/c36 del
   FP 5F6F3ABD (RSX_FP_DUMP=5F6F3ABD) e o[11] del VP 7BC2B163 (RSX_VP_DUMP=1).
   Lo skinning NON e' il problema: la mano "che fluttua" e' il braccio alzato
   del personaggio (posa vera), il resto era l'EQUAL.
2. Lo stallo del report (sopra), 1 corsa su 9.
3. Velocita': 7,5-8 fps nel livello con ~2.900 draw (nessun thread saturo).

### Strumenti nuovi (sessione 30)

- `RSX_DRAW_COALESCE=0` (parole DRAW una per una, come prima).
- `RSX_EQUAL_LEQUAL=0` (torna all'EQUAL vero), `RSX_CULL=0`, `RSX_VIEWPORT=0`,
  `RSX_COLOR_MASK=0`, `RSX_FRONT_FACE=cw`, `RSX_DEPTH_CLAMP=1`,
  `RSX_SHADOW_FORCE=0/1/3/4` (ombra fissa / texel / riferimento),
  `RSX_VP_DBG_FROM=<fotogramma>`, `GCM_ZERO_TRACE=<ms>`; nel drawlist
  `cull=`, `sh=`, `va=`, `/z<zfunc>` sulle texture depth; `[vpstat]` stampa
  anche `o0` del primo vertice trasformato con c0..c3 e la z di viewport.
- `cm=<maschera>` nel `RSX_DRAWLIST`; il messaggio `decompile failed` dice
  quanti byte fino al bit END e il tetto.
- `RSX_DUMP_FRAME` ora non ferma il livello: con `RSX_DRAWLIST_MIN=1500
  RSX_DRAWLIST_AFTER=2450 RSX_DUMP_FRAME=100000` si ha drawlist + dump di
  tutte le superfici del fotogramma 2450 (~65 s, livello pieno); i dump in
  `/tmp/rsx_frame.ppm`, `/tmp/rsx_surf_<offset>.ppm`.

## Sessione 29 — il livello a schermo: costanti FP, NV0039, texture in streaming, normali CMP

Lavoro del 14 settembre (corse `run_s29_c1..c25`, ~60-120 s l'una), non
documentato al momento e ricostruito il 18 dal codice e dai log. Il worker
Gemini ha estratto i numeri delle corse, poi e' caduto (quota): il resoconto
e' fatto sui commenti "sessione 29" nel codice e sui `RSX_FRAMESTAT`.

**1. Costanti dei fragment program fuori dall'hash (passo 0 della s.28,
RISOLTO).** Il titolo patcha le costanti dentro il microcodice FP a ogni draw
(job SPU): la cache per contenuto vedeva un programma nuovo per ogni
variante (8192 piene in ~2.400 fotogrammi, poi tutti i draw con la coppia
incorporata). Ora hash e MSL sono strutturali (`rsx_fp_structural_hash`,
decompilazione in modalita' buffered: `fp_constants[]` in `[[buffer(1)]]`)
e i valori vanno per draw in un'arena da 16 MB (`s_fpc`, `fpc_block`,
blocchi allineati a 256, dedup con il blocco precedente). Misura: **~120 FP
e ~28 VP distinti per corsa**, arena 86-116 KB per fotogramma.

**2. NV0039 (`cellGcmSetTransferData`, copia memoria-a-memoria della RSX)
NON ERA IMPLEMENTATO (RISOLTO).** Uncharted compatta il proprio heap texture
in `func_004D19B8`: un blocco per fotogramma spostato in basso della misura
del buco, con il descrittore aggiornato subito e la copia affidata alla RSX
nell'ordine della FIFO. Senza la copia i descrittori puntavano a dati non
spostati: le texture grandi (cielo, pavimento, sfondo di pausa) uscivano a
rumore con le prime righe di spazzatura (`docs/img_s29_prima_del_nv0039.png`:
cielo a coriandoli verdi, pavimento ciano). Ora `nv0039_copy` in
`cellGcmSys.c` (lineare o per righe, `memmove` perche' sorgente e
destinazione si sovrappongono; `GCM_NV0039=0` la spegne, `[NV0039]` nel
log: 4 copie da 4 MB nella corsa c25).

**3. Texture in streaming servite stantie (RISOLTO).** Il primo bind di una
texture trova l'indirizzo con dati parziali, e la cache per (offset, formato,
misura) serviva per sempre quella decodifica. Ora ogni voce porta
un'impronta dei byte guest (`tex_fingerprint`: 64 campioni da 16 byte, ~1 KB
letto): se cambia, la voce viene ridecodificata; la texture vecchia resta
viva fino a fine fotogramma (`s_tex_graveyard`, i draw registrati la puntano
senza ritenerla). `RSX_TEX_FP=0` spegne il controllo; `redec=` in
`RSX_FRAMESTAT` (9-12 ridecodifiche per corsa).

**4. Normali compresse (RISOLTO).** Il tipo di attributo 6 (CMP: x/y/z a
11/11/10 bit con segno in una parola BE, RPCS3 `RSXVertexFetch.glsl`) non era
gestito in `rsx_vertex_fetch.c`; Uncharted ci mette le normali (attributo 2,
size 1): ogni superficie illuminata usciva a rumore.

**5. Pad.** `LBP_AUTOPRESS=0` ora vuol dire spento (prima contava come
acceso); `LBP_AUTOPRESS_UNTIL=<s>` (in `run.sh`: 28) ferma gli impulsi dopo
s secondi, perche' nel livello START mette in pausa e CROSS fa saltare o
sparare.

**Esperimento aperto: `RSX_EQUAL_LEQUAL=<bias>`.** Il passaggio di colore
usa il test di profondita' EQUAL contro lo z del prepass e su alcune mesh
fallisce (pavimento e fondale mostrano il colore del prepass, mai coperto):
la variabile forza LEQUAL con un depth bias negativo sul draw. Non e' un
fix: la causa (z del prepass diverso da quello del passaggio di colore,
probabilmente per un VP/attributi diversi fra i due) e' da trovare.

**Ritmo delle corse (`RSX_FRAMESTAT=100`):** 2.300-2.800 fotogrammi in
~120 s, 7,4-8,3 fps nel livello con 5.500-6.000 draw per fotogramma;
**`dropped` 200-1.500 draw per fotogramma** (`MTL_MAX_VERTS` = 1M vertici:
il tetto scarta geometria vera, da alzare o da spezzare in piu' command
buffer); c7 e c13 sono cadute con SIGSEGV fuori dalla finestra guest con
`RSX_VP_DBG`/`RSX_VP_DBG_STAT` attivi (diagnostica, non riprodotta senza).

### Fronte aperto (dopo la sessione 29)

1. **Triangoli neri** grandi in alto a sinistra del display (davanti al
   cielo) e **superfici verdi piatte** a destra (ponte/oggetti lontani):
   `RSX_FP_PROBE=all` (colore = hash del programma) sul dump della
   superficie per risalire al programma; poi `RSX_FP_PROBE=<hash>:<espr>`
   per capire quale ingresso porta il difetto.
2. `MTL_MAX_VERTS`: ~1.000 draw scartati per fotogramma.
3. L'EQUAL del passaggio di colore (vedi sopra).
4. Velocita': 8 fps, nessun thread saturo (s.28).

### Strumenti nuovi (sessione 29)

- `RSX_TEXOFF_TRACE=<lo>:<hi>` (`[texoff]`: ogni SET_TEXTURE_OFFSET nella
  finestra, con l'indirizzo FIFO della parola, per risalire con `SPU_WRING` a
  chi l'ha scritta), `RSX_TEX_TRACE=<w>x<h>` (`[textrace]`: ogni cambio di
  offset di una texture di quella misura), `RSX_TEX_SAVE=<n>` (quante
  salvarne) + `RSX_TEX_SAVE_WH=<w>x<h>[/<fmt>]` (solo quelle: gli offset
  cambiano da corsa a corsa), `RSX_TEX_FP=0`.
- `RSX_FP_PROBE=<hash>:<espressione MSL>[;...]` (il programma esce con
  quell'espressione, es. `float4(input.tc0.xy,0,1)`), `RSX_FP_PROBE=all`
  (colore = 24 bit dell'hash), `RSX_FP_DUMP=<hash>` (anche per hash, non solo
  per indirizzo), `RSX_EQUAL_LEQUAL=<bias>`, `RSX_VP_DBG_STAT=1` (min/max di
  ogni attributo su tutti i vertici del draw), `attr_ts` in `RSX_DRAWLIST`
  ((tipo<<4)|size degli attributi abilitati), `GCM_NV0039=0`.

## Sessione 28 — l'assert della job list audio era il nostro kernel SPURS: r4 con il layout sbagliato

**Assert `joblist.cpp(578) m_jobHeaderCommand == kNoJobYet` (thread 14,
audio) RISOLTO: 4 corse su 4 da 120 s pulite, prima 4 su 6 (e 4 su 4 nelle
ultime prove, fra 4 e 60 s dall'avvio).**

Percorso, in ordine:

1. Prima ipotesi (sbagliata, ma la correzione resta perche' e' giusta):
   il commit del PUTLLC copiava tutta la linea da 128 byte, e un store del
   PPU caduto fra il `memcmp` e la `memcpy` veniva annullato dalla copia
   stantia. Ora il commit scrive SOLO i byte che la SPU ha cambiato rispetto
   allo snapshot del GETLLAR (`SPU_PUTLLC_FULL=1` ripristina la copia
   intera). Misura: 3 corse su 3 ancora con l'assert -> non era quello.
2. Strumento nuovo, `SPU_WRING=<ea>:<len>`: anello delle ultime 48
   scritture SPU (DMA PUT/GET e commit atomici) dentro una finestra guest,
   con la catena host (i nomi delle funzioni SPU liftate) e i byte scritti;
   `ppu_guest_trap` (l'assert del titolo) lo scarica insieme alla finestra.
   Job list audio = wid 7, arg `0x108DFD00` (ora `AddWorkload` stampa
   `arg=`). Alla trap: intestazione `00040001` (4 job, 1 preso), slot 0-3
   scritti dal PPU, **slot 4-5 = `FFFFFFFF x4`**, scritti da un **DMA PUT di
   16 byte dal PM** (`P20@B0`, LS 0xB00 -> EA list+0x40, funzione SPU
   0x1E70, chiamata dall'entry del PM 0x2340 con r3 = spuNum e r4 = l'arg
   grezzo). Il PPU accodava il job 4 e trovava lo slot non vuoto.
3. La routine 0x1E70: `biz r4` (word 0), GET di 256 byte da r4.w0, poi PUT
   di 0xFF a `+0x40 + 16*spuNum`: e' la gestione dell'**audit buffer** di
   WwsJob, OPZIONALE, indirizzata dalla **meta' alta** dell'argomento del
   workload. Il kernel SPURS vero (RPCS3 `cellSpursSpu.cpp`:
   `spu.gpr[4]._u64[1] = wklInfo->arg`) passa l'arg come u64 big-endian
   nelle parole 0-1: word0 = alta (0), word1 = bassa (la job list; il PM la
   estrae con `shlqbyi r4,4`). Il nostro `spurs_policy.c` (sessione LBP)
   metteva la EA della lista in ENTRAMBE le parole: word0 != 0 -> il PM
   scambiava la job list per l'audit buffer e ci scriveva sopra.
4. Fix in `runtime/spu/spurs_policy.c`: r4 = {hi, lo, 0, 0}.
   `SPURS_PM_R4_LO=1` ripristina il vecchio layout per riprovare LBP (il
   commento che lo giustificava diceva che con {hi,lo} "l'attach usciva
   subito": e' l'audit che DEVE uscire subito quando non c'e').

**Secondo stallo FIOS RISOLTO: un media thread fermo per sempre in
`cellSpursEventFlagWait` in OGNI corsa (e il salvataggio/menu fermo in 2
corse su 12).** Trovato con `SPURS_EF_TRACE=2A870C80` (nuovo: ogni
Wait/TryWait/Clear/Set del PPU sul flag, con la catena guest): DUE media
thread FIOS (tid 8 e 9, `func_00661438` -> `func_00655300` ->
`func_0009F238`) usano lo stesso decompressore edge-zlib (coda 0x2A870D00,
task 0 del taskset 0x2A86F380, flag 0x2A870C80 bit 0) nello stesso momento,
con il ciclo `while (obj+0x2C00 != 0) cellSpursEventFlagWait(flag, 0x0001,
AND)`: il flag e' solo una sveglia, la condizione vera e' il contatore. Il
nostro Wait bloccava ANCHE il secondo thread e i due si rubavano i
risvegli: chi arrivava per primo consumava il bit di ogni chunk (`waits=0`),
l'altro non vedeva mai il suo e restava fermo con il contatore gia' a zero.
Sul ferro (RPCS3 `_cellSpursEventFlagWait`: `if (ppuWaitMask ||
ppuPendingRecv) return CELL_SPURS_TASK_ERROR_BUSY`) un solo PPU per volta
puo' aspettare: il secondo riceve BUSY subito, rilegge il contatore e
riprova. Ora `cellSpursEventFlagWait`/`TryWait` fanno lo stesso
(`ppu_waiting` in `EventFlagSync`, pausa di 100 us prima del BUSY;
`SPURS_EF_MULTIWAIT=1` ripristina). Misura: **4 corse su 4 da 120 s senza
thread bloccati, tutte con salvataggio e 106 `RequestLevel`** (prima:
thread bloccato in 12 su 12, 91-97 RequestLevel).

**Label della RSX scritti con i byte sbagliati (RISOLTO):** il job di
geometria (ovl 215, `jobmod_008dc900`) chiude ogni blocco di comandi con
`SET_SEMAPHORE_OFFSET 0x480+16*lane` + `TEXTURE_READ_SEMAPHORE_RELEASE
(0x1D74) <posizione nel mesh output ring>` e, prima di scrivere un nuovo
blocco di vertici in `(r12, r5]`, legge il label e aspetta finche' la
posizione della RSX sta DENTRO quell'intervallo (`func_4B48`, ciclo con 25
`rdch Dec` di pausa in `func_4BDC`). Il nostro walker applicava a 0x1D74 lo
scambio di byte 0<->2 del back-end release: il label valeva 0x20000080
invece di 0x20800000, mai nello spazio confrontato. RPCS3 (`nv4097.cpp`):
solo `back_end_write_semaphore_release` (0x1D70) scambia, 0x1D74 scrive
`arg` com'e'. Trovato con `SPU_LABEL_POLL=1` (nuovo). Il watchdog delle
DMA identiche (`SPU_DMA_REPEAT_LIMIT`, default 256 -> 200000) uccideva il
job dopo 256 letture (~1 ms) di un'attesa legittima.

**Perche' il livello va a 3-9 fps (misurato, non ancora risolto):**
`GCM_WSTAT=1` (nuovo): nel livello il walker fa ~500 passate in 2 s, il
95% finisce PARCHEGGIATO su un blocco diverso ogni volta (`[park]`,
`GCM_PARK_DUMP=n`: ogni blocco = JUMP-a-se-stessi + `40580100` (257 NOP)
+ ... + `SET_SEMAPHORE_OFFSET/RELEASE` + RET, patchato dal job) e usa il
5-17% del tempo: la RSX e' PIU' VELOCE dei job, che producono ~600 blocchi
al secondo per lane (~1.6 ms l'uno; su una SPU vera decine di us). La
velocita' del codice SPU liftato e' il limite. Instruments (`xcrun xctrace
record --template 'Time Profiler' --attach`; `sample` sbaglia
l'attribuzione) sulla lane attiva: 58% in `mach_absolute_time` (isb+mrs)
chiamata da `SPU_RdDec` (25 letture per blocco) -- ma con il clock in
cache (`SPU_DEC_CACHE=1`, una lettura ogni 64) i fps non cambiano: da
capire. Le lane attive sono 1-4 a seconda del momento.
`GCM_PARK_POLL_US=200` (ripassare il walker ogni 200 us quando
parcheggiato invece di 4 ms) blocca il livello all'ingresso in 4/4 corse
(put fermo, job in attesa dei label, main thread in attesa dei job): resta
spento (default 0). Anche a 4 ms in ~1 corsa su 3 il livello si ferma dopo
100 fotogrammi: una lane (es. 3) aspetta per sempre che il label esca da
`(20D31500, 20D35600]` con label=20D35200 = fine del penultimo blocco del
giro precedente: l'ultimo blocco di quella lane non e' mai stato consumato
(o il suo label non scritto) prima che il job riavvolgesse il puntatore.
Da capire chi resetta il puntatore (r12 torna indietro a meta' anello) e
perche' il walker non arriva a quel blocco.

**Velocita' delle lane SPU: da ~4.300 a ~45.000 draw al secondo** (misura:
`RSX_FRAMESTAT=100`, draw per 100 fotogrammi / tempo; il livello passa da
4 fps con 1.080 draw a 7-8 fps con 5.500-6.000 draw per fotogramma, che e'
la scena che nel frattempo si e' riempita). Tre cose, tutte misurate con
Instruments sulle lane (`xcrun xctrace record --template 'Time Profiler'
--attach <pid> --time-limit 5s`, poi `xctrace export --xpath
'/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]'` e un
piccolo parser Python del XML; `sample` sbaglia l'attribuzione):
1. `spu_shufb` era il **51%** del tempo delle lane: ciclo a byte con tre
   `if` per byte. Ora NEON (`vrev32q_u8` per portare le parole in ordine
   SPU, `vqtbl2q_u8` sui 32 byte concatenati, tre `vcgeq_u8` per i casi
   speciali): 200.000 vettori casuali identici alla versione scalare.
   Blocchi di comandi per lane: da ~1.600 a ~2.650 al secondo.
2. `spu_ls_read128/write128` + `spu_ls_watch_hit2` = **37%**: una decina di
   sonde (statiche, confronti, probe WWS) su ogni accesso alla LS. Ora un
   percorso veloce (`g_spu_ls_diag`, deciso una volta dalle variabili
   d'ambiente `LBP_SPU_WATCH SPU_LS_LOWREAD SPU_LS_WRITE_WATCH
   SPU_LS0_WATCH SPU_SMC_WATCH SPU_WWS_CMDS SPU_WWS_STATE SPU_LS_DIAG`):
   load/store NEON + inversione dei byte. Blocchi: ~3.600 al secondo.
3. Il kernel SPURS serializzava i workload su un thread: il synth audio
   (wid 7, ~1.700 corse al secondo, 0,5 ms l'una) prendeva il **45%** del
   tempo e la geometria (wid 12, 5 lane) aspettava (`SPURS_LANE_STAT=1`,
   nuovo). `SPURS_KERN2_WIDS=<maschera>` (in `run.sh`: 0x80) fa girare
   quei workload su un secondo thread kernel. Provato prima
   `SPURS_SKIP_WID=80` (non dispatchare l'audio, solo per misurare): 9,5 fps
   contro 4.
4. ~16% delle lane in creazione/distruzione di thread (`CreateThread` per
   le lane 1..4 a OGNI corsa del PM, ~7.000 thread al secondo): ora ogni
   thread kernel tiene un pool di worker persistenti (condition variable
   per consegnare il lavoro e per il "fatto"; `SPURS_LANE_POOL=0` torna a
   CreateThread). Blocchi per lane: ~5.000 al secondo.
5. Con i job veloci il livello si fermava dopo ~100 fotogrammi in
   `cellGcmSetWaitFlip`: `GCM_FLIP_FIFO` completava il flip solo a
   get == put, ma il walker, passata la parola di flip di N, entrava nel
   segmento di N+1 e parcheggiava su un blocco che il PPU kicka solo DOPO
   la WaitFlip. Ora un flip che viene dalla parola 0xFEAD nella FIFO (il
   walker ha gia' consumato tutto N) si completa senza aspettare put; il
   vincolo resta per il flip via import. 150 s di livello senza fermate,
   7-8 fps con 5.300-6.600 draw per fotogramma.
Il trace `[spurs-pm] poll #N` usciva 8.000 volte al secondo (contatore
azzerato a ogni corsa): ora e' su un contatore mai azzerato.
6. Altri tagli misurati con Instruments: azzeramento parziale del contesto
   "fresh" del PM (solo LS 0..0x4000 + campi di stato, `SPURS_PM_FRESH_LS`;
   era bzero di 256 KB ~10.000 volte al secondo, 5,6%), `_setjmp` al posto
   di `setjmp` in `spu_run_with_halt` (sigprocmask a ogni corsa, 5,6%),
   percorso veloce in `spu_task_launch_check` (6%), cache di `vp_lookup`
   per generazione del microcodice (`vp_ucode_gen`: l'hash FNV su 8 KB a
   ogni draw era il 42% del thread del walker), cache delle texture da
   2.048 a 32.768 voci con indice hash e chiave senza i bit del numero di
   mip (il livello lega ~17.000 texture piccole distinte: con 2.048 voci
   ridecodificava 1,3 Gpixel ogni 100 fotogrammi; `RSX_TEXCACHE_DBG=1`,
   `decodifiche=` in RSX_FRAMESTAT). Blocchi per lane: ~8.000 al secondo.
   Livello: 8,5-9,5 fps con 5.500-6.700 draw. Nessun thread e' al 100%
   (lane ~65%, main 35% -- di cui i due terzi nell'attesa dei job --,
   walker 25%): il resto e' latenza fra i tre.
7. `SPURS_KERN2_WIDS` accetta piu' maschere ("80,1000": audio, geometria e
   il resto su tre thread kernel): wid 12 passa da 4.400 a 5.400 corse al
   secondo ma i fps non cambiano -- le lane non sono piu' il collo. Il
   walker parcheggia ~10 volte per fotogramma e ogni park costa un tick da
   4 ms (~40 ms dei ~110 ms di fotogramma): ma `GCM_PARK_POLL_US=200`
   blocca ancora il livello all'ingresso (anche col flip corretto): una
   lane aspetta per sempre che il label esca da un blocco che contiene la
   posizione finale del giro precedente (es. lane 2: label 20C5C380 in
   (20C59A00, 20C5E700]) mentre il walker e' parcheggiato sul PRIMO blocco
   del fotogramma, che deve scrivere proprio quel job. Con il walker a
   4 ms la posizione della RSX e' vecchia e il controllo non scatta.
   CAPITO E AGGIRATO: ogni fotogramma il mesh output ring riparte dalla
   base (un comando scrive label = base, poi i blocchi salgono); i blocchi
   consecutivi dello stesso job si SOVRAPPONGONO (il label del blocco k e'
   la fine RISERVATA, il blocco k+1 parte dalla fine usata, piu' bassa).
   Sul ferro non e' mai un problema perche' la RSX consuma il fotogramma
   PRIMA mentre i job scrivono quello dopo: la sua posizione non e' mai
   dentro un blocco in scrittura. Il nostro walker consuma i blocchi
   appena patchati e "raggiunge" il job: il job aspetta che la RSX consumi
   un blocco che deve scrivere lui stesso. Ritardare i label
   (`GCM_LABEL_DELAY_US`, 2 e 6 ms) non basta (il label vecchio di un
   blocco precedente sta comunque dentro la riserva). Rimedio nel runtime
   SPU (`spu_dma.h`, `GCM_LABEL_UNSTICK_MS`, default 20): se una lane
   legge lo stesso label con lo stesso valore, dentro il proprio blocco,
   per piu' di 20 ms, il label viene portato a r12 (inizio del blocco,
   escluso dall'intervallo): nel port e' vero e innocuo, il walker ha gia'
   copiato i vertici dei blocchi consumati. Con `GCM_PARK_POLL_US=200`
   ora il livello parte e va (8 sblocchi in 150 s), ma i fps sono gli
   stessi del tick a 4 ms: il park non era il collo. Con i default:
   **420 s di livello senza fermate, 8 fps costanti con 5.300 draw**, 0
   sblocchi necessari. Il walker riparte da solo anche dopo la parola di
   flip (prima aspettava il prossimo `put`).

**LA PRIMA SCENA 3D DEL LIVELLO** (`docs/img_s28_dive_boat_007D0000.png`,
fotogramma 2700 della corsa c131, `RSX_DRAWLIST_MIN=3000
RSX_DRAWLIST_AFTER=2700 RSX_DUMP_FRAME=1`): nella superficie principale
007D0000 (A8R8G8B8 MSAA 2x, 2560x720) c'e' il ponte della dive-boat --
gru a sinistra, parapetti, cime, la struttura della barca -- in
silhouette nere su cielo azzurro con triangoli verdi, e un quad verde sul
ponte. Anche 03EE0000 (ombre 2432x1216), 031E0000 e 034B0000 hanno
contenuto. Il display resta azzurro uniforme perche' il quad finale
(#6134, fp 00125402) legge 016D0000 (fmt 0x14B FP16, MRT ct=0x17 con
01E50000/025D0000: 884 draw, il passaggio di illuminazione differita)
che e' PIATTO, e piatto e' anche 02D50000 (il depth risolto in ARGB dal
quad del titolo, letto da quei draw come `tex 3:1280x720@2D50000/fA5`).
Il sospetto e' la catena depth di 007D0000 (Z=F50000, MSAA 2x) -> quad
di risoluzione -> 02D50000 -> illuminazione: se il depth risolto e'
costante, l'illuminazione produce un colore costante. Prima misura da
fare: `RSX_DEBUG_TINT=3` / dump di 02D50000 e del depth F50000 in un
fotogramma del livello (`[RSX metal] depth 0x..: z min/max`).

**Lo schermo del livello era un azzurro uniforme** (corsa da 600 s,
`RSX_DUMP_FRAME=300 RSX_DUMP_SKIP=1200`: fotogramma a 169 s, media RGB
83/186/225): il fade nero del caricamento se n'e' andato, ma con 5.300
draw il display mostrava un colore piatto. `RSX_DRAWLIST_MIN=3000`: tutti i
6.301 draw avevano `vp=-` (nessun vertex program del guest) -- la **cache
dei fragment program era piena (512 voci)** e da li' in poi ogni draw
usava la coppia incorporata: tutte le superfici (007D0000 MSAA, 016D0000
MRT, 03EE0000/044D0000 ombre 2432x1216, 00050000 display) uscivano dello
stesso colore piatto. Ora: cache dei FP e delle pipeline a 8.192 voci con
indice hash (`RSX_FP_CACHE_MAX=<n>` per l'A/B), e **compilazione dei
fragment program ASINCRONA** (coda GCD a priorita' utility; la voce entra
in cache subito senza libreria, i draw intanto usano la coppia incorporata;
`RSX_FP_SYNC=1` per la sincrona): il livello ha 2.500+ programmi e ogni
`newLibraryWithSource` costa 50-100 ms sul thread del walker (misurato:
passate da 5 s, 85% nel compilatore). NON ANCORA VISTO il risultato a
schermo: con i programmi veri il livello si ferma spesso all'ingresso
(fotogramma ~2100, 45 s): `BATCHJOB: Waiting for batch job list 0 to
finish` sul thread principale, lane e walker fermi. Correlazioni misurate
(1 corsa ciascuna, quindi da confermare): con `RSX_FP_CACHE_MAX=512` va;
con `RSX_OCCLUSION=0` (report a valore fisso) va in 2 corse su 3; con
`RSX_DUMP_FRAME` impostata si ferma sempre (il percorso del dump nel
present esce prima del commit/handler dei report). `run.sh` ora ha
`RSX_OCCLUSION=0`. Il watchdog "stuck-get" del walker (8 passate = 32 ms)
scattava su park legittimi e saltava a `put` (label e report del
fotogramma mai scritti): ora ignora i park e vale 250 passate. Trovato e
risolto anche un `bisl $r0,$r20` del job 216 verso 0x6C80/0xA020 (tabella
sparsa a LS 0x14060) non liftato: `lift_jobmods.py` ora aggiunge come
ingressi extra le tabelle di puntatori (corse di >= 6 parole, o >= 2 con
zeri sparsi); il microstep riconosce `hbra/hbrr` (op7, non op9). Il codice
SPU liftato si compila ora a -O2 (`SPU_OPT` in `build.sh`; +30-60% di
blocchi), e `build.sh` ricompila i moduli liftati quando cambia un header
di `runtime/spu` (prima un campo aggiunto a `spu_context` dava "Illegal
instruction" all'avvio; i campi nuovi vanno comunque IN CODA alla struct).

**Occlusion query vere (lavoro della sessione precedente, non ancora
raccontato, ora misurato):** `NV4097_GET_REPORT` ZPASS -> visibility buffer
Metal (`MTLVisibilityResultModeCounting`, uno slot da 8 byte per draw,
anello di 3 buffer), `CLEAR_REPORT_VALUE` segna l'inizio; il completed
handler somma gli slot e scrive il conteggio vero a ea+8
(`rsx_report_set_value`). Nel livello: **216 report per fotogramma, 124
visibili** (`RSX_OCCLUSION_DBG=1`); nel menu 4 report. `RSX_OCCLUSION=0` lo
spegne (valore fisso). Il flip (`cellGcmTickFlip`, `GCM_FLIP_FIFO`) aspetta
che la GPU abbia finito il fotogramma con i report, perche' il job 215 li
legge subito dopo il flip; il present ora sta DOPO il flip nel ticker.
Costo misurato e ripagato: rimandare il flip al tick dopo faceva 33 fps nel
menu contro 55 senza occlusion; ora `rsx_metal_backend_gpu_wait(8 ms)` nel
flip + **present su un secondo command buffer** (cosi' il completed handler
dei report non aspetta il vsync del drawable): 2700 fotogrammi in 54 s
(~50 fps) contro 2900 senza occlusion.

**Kernel SPURS senza dormire fra le passate** (idem, sessione precedente):
`SPURS_KERN_SPIN` (default 1): se l'ultima passata ha trovato lavoro la
prossima parte subito (`sched_yield`), altrimenti pausa di
`SPURS_KERN_SLEEP_US` (default 200; 1000 = il vecchio `Sleep(1)`).

**Ritmo del gioco ora** (`RSX_FRAMESTAT=100`, corse `run_s28_c9..c15`):
autosave/loghi/menu ~50 fps con ~19 draw; dal fotogramma ~2200 (63 s) il
livello dive-boat: 376 poi **1094 draw per fotogramma a ~3 fps**, 97
`RequestLevel` in 120 s. E' la fase lenta gia' misurata (PPU che spinna sul
job manager in `func_0074232C`): resta il fronte.

**LA SCENA VERA (13 settembre, sera tardi, corse c131..c136): il display
era piatto perche' NESSUN draw usava il vertex program del guest.** La
catena "02D50000 costante -> 016D0000 piatto" era un sintomo: nel
fotogramma con drawlist (c131, #2700) tutti i 6161 draw avevano `vp=-` e
`0 blocchi di costanti`, cioe' `vp_lookup` tornava -1 per tutti e la
geometria passava dal VS incorporato (posizione grezza, texcoord e colori
a caso: silhouette nere, cielo (83,186,225) su OGNI superficie compresa la
copia del depth). Due cause in fila:
1. `s_vps[256]` era pieno (dopo ~150 fotogrammi del livello) e il 257-esimo
   programma tornava -1 senza dirlo. Portato a 4096 con messaggio: pieno lo
   stesso.
2. L'hash del VP copriva `vp_ucode_bytes - start`, cioe' TUTTI i byte
   caricati dopo lo slot di partenza -- anche gli altri programmi residenti
   (Uncharted ne tiene diversi nei 512 slot, `start` 4256/4528/5072/5136...).
   Ogni ricarica di uno slot vicino cambiava l'hash di ogni VP: 4096 voci
   "diverse" per 25 programmi. Ora l'hash si ferma al bit END
   (`rsx_vp_program_size_instrs`): **25 VP nella corsa c136**, 4072/4072 draw
   con VP, 365 blocchi di costanti.
Risultato (dump del fotogramma #2162 di c136): 007D0000 ha 7071 colori
distinti con la barca, la cabina, il molo e le montagne a fuoco
(`docs/img_s28_dive_boat_c136.png`, colori FP16 clampati 1:1, non tone-map);
02D50000 ha 5327 valori (byte del depth, (255,255,0) = lontano, B=0 come
scrive `depth_to_rgba`) -- il quad di risoluzione funziona.
Attenzione a `build.sh`: due corse (c133/c134) sono state fatte col binario
vecchio perche' avevo lanciato solo `cmake --build`; `run.sh` usa il
binario linkato da `build.sh`, sempre entrambi.

Aperto subito dopo, nella stessa corsa: **cache dei fragment program PIENA
(8192)** e delle pipeline (8192) a ~frame 2450: il titolo patcha le costanti
dentro il microcodice FP a ogni draw (job SPU), e la cache e' per contenuto
-> ogni variante e' un programma nuovo. Va tolto il valore delle costanti
dall'hash e passato come buffer (il decompilatore le incorpora come
letterali). Inoltre `MTL_MAX_VERTS` (1M) scarta ~860 draw per fotogramma
(`dropped ... (cap 32768 draws / 1048576 verts)`). E 016D0000 (luce
differita) esce nero con i VP giusti: da guardare dopo le costanti.

### Il prossimo passo (sessione 28)

0. (risolto: era l'hash del VP, vedi "LA SCENA VERA") Ora: costanti FP
   fuori dall'hash (cache piena a 8192), `MTL_MAX_VERTS`, poi 016D0000 nero.
1. (risolto, vedi sopra: era il watchdog) Lo stallo all'ingresso del
   livello con i fragment program veri: il PPU spinnava su `0x202FFED8`
   (`[HOTREAD]`, lr=0x005D78C8) aspettando che diventi != 0. Quella parola
   la scrive il job **ovl 210** (pc 0x6098, `PUT` di 16 byte a 0x202FFED0
   = {1, 0, 1, 0}; visto con `SPU_WRING=202FFE00:100 SPU_WRING_LIVE=1`,
   11 scritture nella corsa) -- quindi la scrittura c'e' stata e il PPU
   l'ha poi azzerata e ha kickato un nuovo batch che il job non ha piu'
   servito, oppure il job ha scritto prima del reset. Le job list di wid
   14/15 (`SPURS_DISPATCH_TRACE=1`) risultano consumate (1/1 e 6/6 job
   presi, lane FFFF). Da fare: tracciare l'ordine PPU (reset di
   0x202FFED8 + kick) vs job 210 (scrittura), con `YDKJ_WWATCH=0x202FFED0`
   per le scritture PPU e `SPU_WRING_LIVE` per quelle SPU.
   Poi guardare finalmente la scena: `RSX_DUMP_FRAME` va riparato prima
   (fa fermare il livello: il percorso del dump nel present esce prima del
   commit e degli handler), oppure catturare la finestra da fuori
   (`screencapture -l <windowid>`; `screencapture -x` prende lo schermo
   intero e la finestra del gioco puo' essere su un altro spazio).
   RISOLTO (22:35): nelle corse ferme c'era sempre un `[spu-dma] ...
   200000 identical GET ... halting the SPU` sul label di una lane: il job
   aspettava in una TERZA forma (ne' "dentro (r12, r5]" ne' il ramo di
   wrap), lo sblocco non scattava e il watchdog uccideva il job -> blocco
   mai patchato -> walker parcheggiato per sempre -> "Rendering is stuck
   on reference value". Ora lo sblocco dopo 20 ms vale in ogni caso
   (dentro: label = r12; wrap `r5 > r26 && label <= r23`: label = r23+16;
   altro: r12). Con programmi veri + occlusion vere: 3 corse su 3 senza
   fermate (2.800 fotogrammi in 125 s, 8 fps con 5.000-6.000 draw).
   `run.sh`: `RSX_OCCLUSION=1`, `RSX_FP_CACHE_MAX=8192`.
2. Velocita': ~8 fps con 5.300 draw; nessun thread saturo (lane ~65%, main
   35%, walker 25%): e' latenza fra PPU, job e walker. Prossima misura:
   la catena critica di UN fotogramma (timestamp di kick job list, fine
   job list, put, flip).
2. Corsa lunga (600 s) con `SPURS_IDLE_MAX=0`: il caricamento finisce
   (regioni in stato 5, fade via)? Screenshot con `RSX_DUMP_FRAME` a 300+ s.
3. Con `SPURS_PM_R4_LO` spento, riprovare LBP (lbp/) prima di
   risincronizzare con l'upstream: il layout {lo,lo,hi,hi} era nato li'.

### Strumenti nuovi (sessione 28)

- `SPU_WRING=<ea>:<len>` (anello scritture SPU in una finestra, scaricato
  alla trap del titolo; `[wring]`), `SPU_PUTLLC_FULL=1`, `SPURS_PM_R4_LO=1`,
  `arg=` in `[cellSpurs] AddWorkload`.
- `SPURS_EF_TRACE=<ea>` (`[ef-trace]` + `[GCS:ef-wait/ef-clear]` catena
  guest), `SPURS_EF_MULTIWAIT=1`.
- `SPU_LABEL_POLL=1` (`[label-wait]`: strisce di letture dei label 0x480..
  per lane, con blocco e conteggio `rddec`), `SPU_REPORT_POLL=1`
  (`[report-poll]`: letture dei report 0x21014000..), `SPU_WRING_LIVE=1`
  (le PUT nella finestra di `SPU_WRING` subito), `SPU_DEC_CACHE=1`,
  `SPU_DMA_REPEAT_LIMIT` (default 200000).
- `GCM_WSTAT=1` (`[wstat]` ogni 2 s: passate, ms nel walker, park, get/put),
  `GCM_PARK_DUMP=<n>` + `GCM_PARK_DUMP_FROM=<frame>` (`[park]`: le parole
  attorno al blocco parcheggiato), `GCM_PARK_POLL_US=<us>`.
- I contatori del trace `[spurs-pm] DMA` sono atomici: prima il tetto non
  arrivava mai e una corsa da 10 minuti ha scritto 30 milioni di righe.
- `RSX_OCCLUSION=0`, `RSX_OCCLUSION_DBG=1` (`[occlusion] N report, M
  visibili` per fotogramma), `SPURS_KERN_SPIN=0`, `SPURS_KERN_SLEEP_US=<n>`.

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

### Terza parte: dal menu al primo livello

9. **I glifi.** L'atlante 256x128 (fmt 0x85, ARGB8) e' decodificato bene:
   ha 4 pagine di glifi nei 4 canali (`RSX_TEX_SAVE_OFF=0601AF00`), e il
   fragment program sceglie il canale con `dot(texel, tc2)` (tc2 = attr4 =
   maschera). Il registro remap della texture (SET_TEXTURE_CONTROL1) era
   0xAA1B, non l'identita' 0xAAE4: senza remap usciva il canale sbagliato.
   Ora `tex_with_remap()` crea una vista Metal con swizzle (cache per
   texture+remap). Il menu si legge.
10. **Input.** `LBP_AUTOPRESS` (CROSS+START ogni 240 letture) fa partire
    "New Game" a caso; ora `PAD_SCRIPT="45:x"` (pressioni a tempo) e la
    tastiera della finestra Metal come pad 0 (`rsx_metal_key_down` <-
    `cellPad.c`): frecce, Spazio/X CROSS, C CERCHIO, Z QUADRATO, V TRIANGOLO,
    Q/E L1/R1, F/R L2/R2, Invio START, Tab SELECT, WASD/IJKL levette.
11. **Salvataggio.** "New Game" chiama `cellSaveDataListSave2` (setBuf da
    480 byte = 10 directory, funcList/funcStat/funcFile). Riscritta con le
    callback guest (`savedata_list_guest`, anche ListLoad2); le strutture
    stanno nello scratch HLE (`VM_HLE_INJECT_BASE + 0x8000`: la base fissa
    0x024E0000 di prima era dentro la memoria del titolo). "SAVE completed",
    directory `BCJS30015_NDI_UNCHARTED_DF_0` con PARAM.TXT.
12. **Il livello.** `dive-boat` carica (21 MB), poi crash: SIGBUS a
    0xD0000000 da `NavMesh::FindNearestPolyL` (0x455268) -> 0x6A0D78 con
    un puntatore 0xCFFFFFFF. Il rapporto di fault ora stampa tutti i gpr,
    v0-v3 e lo stack: le coordinate quantizzate del box erano x0=5 > x1=0.
    Causa: **`vminfp`/`vmaxfp` liftate con `float*` sul register file
    big-endian** (confronti su byte invertiti). Sistemate insieme alle
    altre op intere VMX per corsia che facevano lo stesso (vmsumshm,
    vadduhs/vaddshs/vadduws, vsubsws, vsum2sws, vmulouh/vmuleuh, vpkshss,
    vmsummbm: ora `ppu_vldu8h/ppu_vstu8h/ppu_vldu4/ppu_vstu4`).
13. Con quello il livello finisce il login di tutti gli attori ("pir-smg-*",
    "anim-dive-boat", "Sound bank 'dive-boat'"). Poi:
    - il PM finiva in `BRANCH-TO-0 pc=0x03528` dopo un interrupt preso
      nella finestra `brsl $r0,0x3600` (ritorno via `iret` a un indirizzo
      non registrato): `--extra-funcs 0x3528` in `relift_spu.sh`.
    - il job ovl 218 (`jobmod_00903d80`, 41 KB, senza stringhe, gira su
      piu' lane insieme) scrive `(32DF0000 x3, 0)` a **LS 0** (il vettore
      di interrupt del PM: `bra 0xA2C` installato dal PM stesso a 0x2D74)
      da pc 0x5CC0 via `stqx $rN, $r3, $rM` con r3 = `*(r3+0x20)` = 0:
      un puntatore nullo nei suoi dati. Da li' gli interrupt non vengono
      piu' presi ("LS0 word 0x32DF0000 is no branch"), il PM salta nei
      propri dati (stopd @0x15B4/0x15EC = area di salvataggio registri) e
      la FIFO resta parcheggiata ("stuck-get"). Nella traccia del job
      (`SPU_JOBTRACE_OVL=218 SPU_JOBTRACE_ONE=1`) ci sono anche dispatch
      "(no lift)" a 0xCBCC/0xCD54/0xCD7C/0xD2F4 = i ritorni di `bisl
      $r0,$r0` (chiamate indirette a puntatori letti dai dati del job).
    - `build.sh` non ricompila i moduli SPU liftati quando cambiano SOLO
      gli header (`spu_context.h`): `rm obj/spu_*.o` prima.

14. **Il job 218 e' il job di geometria di ICE** (`SPU_DMA_DUMP_EA=
    2B053CE0,2B053CD0,25EEEC10`): la sua lista DMA (0x60 byte, coppie
    size/EA) carica in LS 0x16000+ un blocco parametri da 16 byte
    (`07000930 0 0 60`), sei coppie di record (0xA0 + 0xC0 byte: descrittori
    di stream di vertici ICE -- `00160008 000C0020...`, half 0x3C00) e un
    blocco da 0x400. Lo store a LS 0 sta nella funzione 0x5C48: `rotqby
    $r9, $r4, $r3` sceglie una corsia di r4 (una quadword di **indirizzi
    LS di buffer**, quelli che il job chiede al PM con le API 0x1C20/
    0x1BDC/0x1D20 = get-buffer), `a $r3,$r9,$r3`, `lqd $r3, 0x20($r3)`,
    poi `stqx` da r3. Con r4 = 0 (buffer non assegnati) r3 = 0. Il job non
    scrive mai a LS 0x16030 (`SPU_LS_WRITE_WATCH=16030
    SPU_LS_WRITE_WATCH_OVL=218`), quindi non e' un campo patchato dal job.
    Sospetto: la nostra esecuzione del PM restituisce buffer nulli a questo
    job (bufferSetArray / load commands del suo descrittore).
15. `find_spu_functions.py`: l'istruzione dopo ogni `bisl` e' un ingresso
    (i "(no lift)" a 0xCBCC/0xCD54/0xCD7C/0xD2F4 sono spariti).

### Quarta parte (notte 12→13 settembre): i primi draw dei job SPU

Il filo era "perche' il job 218 riceve buffer a zero"; la risposta e' stata
una catena di bug del lifter/runtime SPU, tutti corretti e misurati.

16. **`bisl $rN,$rN` chiamava se stesso.** Il lifter scriveva il link nel
    registro PRIMA di leggere il bersaglio: `bisl $r0,$r0` (il job 218 chiama
    la sua tabella di API attraverso r0, 11 casi nel modulo) saltava a
    `pc+4`. Per questo la seconda `GetBufferTag(set 1, buf 0)` non veniva
    mai eseguita e il "risultato" era `andi $r3,$r2,4` = (4,0,0,0) rimasto
    in r3: handle 0 → store a LS 0 (`stqx` in 0x5C48 con base
    `LS[0xECA0]=0x00100000`). Stessa cosa per `bisled`. Il PM ora restituisce
    (0x01000000, 0x98004000, ...) = pagina 0x98 → LS 0x26000, giusto.
17. **`bi $r82` con bit 0 acceso.** Il job 215 (geometria) tiene un flag nel
    bit 0 del selettore della sua tabella di salto (`bi $r82` = 0x12231):
    `spu_indirect_branch` ora maschera `~3` come l'hardware (prima:
    BRANCH-TO-0 a pc=0x12231 e stack del job sopra 0x3E000 → assert del PM a
    0x15F0/0x165C, magic 0x71077345 sporcato).
18. **`bi $r0` con r0 != indirizzo di ritorno non e' un ritorno.** Il PM fa
    `ila $r0,epilogo; br helper` e l'helper finisce con `bi $r0`; SPU_RET lo
    prendeva per ritorno del frame host e saltava l'epilogo (r1, r80, r81
    mai ripristinati: il job 218 usciva senza disegnare, e nel job 227 audio
    tutto il codice dopo 0xFFAC non girava mai). Ora `SPU_RETR(ctx, reg)`
    confronta con `ret_link` (piantato da brsl/bisl): se diverso, dispatch.
    `SPU_RET_LOOSE=1` ripristina il vecchio comportamento. Il registro e'
    quello del `bi` (`bi $r78` del PM: prima SPU_RET leggeva r0).
19. **Interrupt annidati.** Con (18) il gestore di interrupt del PM (0xA2C)
    arriva alla finestra `bie` del suo helper (0x2430) con l'evento ancora
    pendente: TAKE annidato (misurato: due TAKE a srr0=0x2430 di fila). Il
    runtime ne teneva UNA istantanea di registri: la seconda sovrascriveva
    la prima e l'iret esterno ripristinava i registri del gestore
    (`r4=0x24F8` al posto del link 0x2E18 → loop 0x2430/0x2438/0x24F8 con
    r1 = spazzatura, gioco fermo dopo 'world-1'). Ora e' una pila di 4
    (`irq_gpr[SPU_IRQ_NEST]`): l'hardware vero salva 2032 byte per livello
    sullo stack guest, quindi l'annidamento e' previsto dal PM.
20. **`cflts/cfltu/csflt/cuflt`: fattore di scala sbagliato.** Il runtime
    faceva `2^((173-i8)-155)` e `2^((155-i8)-155)`: con le codifiche normali
    (cflts 173, csflt 155) OGNI conversione float↔int moltiplicava per
    2^-155 = 0. Giusto (CBEA, rpcs3 `scale[173-i8]`/`scale[i8-155]` con
    scale[x]=2^x): `2^(173-i8)` e `2^(i8-155)`. Il culling dei triangoli
    del job 215 (funzione 0x10300, `cflts $r,$r,172` sui vertici proiettati)
    vedeva tutto degenere: `ctx+0x114` (numero di indici) sempre 0 →
    `brz $r13, 0x8E20` a 0x8B44 saltava l'emissione del draw, e il pacchetto
    restava intestazione (1710/1680/181C/1820) + buco di zeri + semaforo.
    Verificato con un test host (`cflts 173: 1.5→1, 100→100; csflt 144:
    32768→16`).
21. **`NV4097_TEXTURE_READ_SEMAPHORE_RELEASE` (0x1D74)** ora scrive il label
    come 0x1D70 (stesso scambio di byte; rpcs3 fa lo stesso). Uncharted usa
    SOLO questo: i pacchetti dei job finiscono con `SET_SEMAPHORE_OFFSET
    0x480..0x4C0 + release`, e il job 215 legge i label 0x500004A0.. con
    GET da 4 byte (anello dei vertex buffer).

**Misura** (`run_s27_c140.log`, 80 s, con `PS3_TTY_POKE=100ADC80:0`, vedi
sotto): **133 buffer CALLati con draw su 3000** (`[callstat]`: 1808×266,
1824×142) — i primi draw dei job SPU. A schermo: un rettangolo nero su
verde (`c141_160.png`). Senza il poke: 0 draw (il job aspetta i report).

**Il blocco che resta: i report di occlusione.** Il job 215, per ogni
oggetto, aspetta (`0x86F4`, GET da 16 byte a 0x21014410/420/470/480/530)
che la parola +8 dello slot sia != -1 (`0x8754`: 0 → flag "occluso" →
0x8CD8; altro → disegna). Sono **report RSX in memoria principale**
(`SET_CONTEXT_DMA_REPORT=0xFEED0001`, tabella di 4096 slot da 16 byte a IO
0x14400 = EA 0x21014400, `{timestamp, timestamp, valore, 0}`). Il PPU in
SwapBuffers (0x14F7F4) scrive -1 nel +8 di TUTTI i 4096 slot a ogni
fotogramma se il bool `*(0x100ADC80)` (statico = 1, tabella a 0x100ADC60 di
descrittori+flag) e' acceso. Chi li scrive? Non i job SPU (nessun modulo
costruisce 0x1800; i buffer CALLati non ne contengono), non il PPU (i suoi
`GET_REPORT` per fotogramma — `func_00703068(ctx,type,off)`, 16 chiamanti —
vanno a 0x244E0-0x24550, cioe' base+0x10000, e all'avvio una volta a
0x144B0-0x14530). `NV4097_GET_REPORT` (0x1800) non e' implementato nel
walker. Ipotesi: doppia pagina di report (0x14400 / 0x24400) e il flip non
avviene, oppure il PPU la resetta a ogni fotogramma solo in questo build.
Il **poke** `PS3_TTY_POKE=100ADC80:0` (spegne il reset) sblocca il job: i
valori restano 0 (memset) → path "occluso" per molti, ma 133 disegnano.
Da fare: implementare GET_REPORT (valore != 0 per ZPASS finche' non c'e'
l'occlusion query in Metal) e capire il flip.

**Altre cose viste stanotte:** il livello ora si carica da solo a ~50 s
senza premere X (`PAD_SCRIPT` resta per sicurezza); la finestra si trova
con `CGWindowListCopyWindowInfo(.optionAll)` (con `.optionOnScreenOnly` non
si vede: `scratchpad/winid.swift` + `snaprun.sh <log> <prefix> <t1,t2,..>`
in `/private/tmp/claude-501/.../scratchpad`); il job 206 scrive i buffer
"vuoti" (zeri + RET), 217 il park, 215 il pacchetto; 218 (ICE vertex) gira
solo ~8 volte al caricamento del livello.

22. **`NV4097_GET_REPORT` (0x1800) e `SET_CONTEXT_DMA_REPORT` (0x01A8).** Il
    walker scrive {timestamp, valore, 0} all'EA del report (IO -> EA per
    0xFEED0001); il conteggio ZPASS e' fisso `RSX_ZPASS_COUNT` (default
    0x1000: "visibile") finche' non c'e' l'occlusion query in Metal. Trovato
    con `PS3_TTY_SCAN=01014400:FFFFFF00` (con il poke attivo): il PPU emette
    `GET_REPORT slot k; CLEAR_REPORT_VALUE; CALL <buffer del job k>` per ogni
    oggetto del passaggio principale, e il job 215 del fotogramma dopo legge
    lo slot (passaggio ombre compreso, che nell'anello viene PRIMA). Sul
    ferro funziona perche' la GPU sta un fotogramma dietro il PPU: il -1 di
    SwapBuffers viene ricoperto dalla GET_REPORT del fotogramma prima. Il
    walker sincrono aveva gia' scritto il valore che il PPU cancellava, e il
    job si bloccava sul park. Rete di sicurezza in `spu_dma.h`: una GET SPU da
    16 byte che trova -1 in uno slot gia' scritto dal walker riceve l'ultimo
    valore (`rsx_report_shadow_get`, `RSX_REPORT_LAG=0` lo spegne); nella
    corsa di misura non e' mai scattata.

**Misura (`run_s27_c149.log`, 200 s, nessun poke):** 104-123 buffer con draw
ogni 500 CALLati, ~18 draw/fotogramma in piu' del post-processing, e **la
prima scena 3D**: la sagoma della barca del livello 'dive-boat' con la scia,
nera su verde, in basso a sinistra (`docs/img_s27_prima_scena_3d.png`).
Manca tutto il resto: illuminazione/texture (i draw dei job escono neri),
posizione/viewport (la scena occupa il quadrante in basso a sinistra, un
rettangolo nero in alto a sinistra), depth test.

### Quinta parte (13 settembre): depth, MRT, e perche' lo schermo resta nero

Commit `0026542` (backend Metal, non ancora raccontato qui):

23. **Superfici e depth nel backend Metal.** Dimensioni per superficie
    (`surface_texture(off,w,h,pf)`, 64 slot, drawable ridimensionato SOLO
    per le superfici display: l'atlante delle ombre lo portava a 2432x1216);
    passaggio principale 007D0000 = A8R8G8B8 MSAA 2x (fmt 0x3148, pitch
    10240 → 2560x720); depth F50000 Z24S8 come attachment vero, test/scrittura
    da `dt=test/func/mask` (`dss_for`, `RSX_DEPTH_FUNC` override, `RSX_DEPTH=0`);
    **export del depth dal fragment program** (`SHADER_CONTROL & 0xE`, r1.z →
    `[[depth(any)]]`): senza, il quad del cielo/copy (control 0x200840E)
    lasciava il depth a zero e il z-prepass (LEQUAL+write, poi colore EQUAL)
    scartava tutto. Il titolo "risolve" il depth con un quad che scrive Z
    codificato in ARGB (A=Z[23:16], R=Z[15:8], G=Z[7:0]) nella superficie
    02D50000, poi usata come zeta: `zeta_from_color` (compute
    `fs_rgba_to_depth`) e `depth_to_rgba` (`depth2d<float, access::read>`,
    con `texture2d` non leggeva nulla) tengono le due viste sincronizzate.
    MRT: `SET_SURFACE_COLOR_TARGET`=0x17 → 3 attachment (016D0000/01E50000/
    025D0000, FP16 fmt 0x14B → `RGBA16Float`, `RSX_HDR=0` lo spegne), il
    decompilatore emette `struct RsxFpOut { color0..3, depth }`
    (`rsx_fp_set_mrt_count`). Lookup delle texture: prima le superfici di
    rendering (il post-processing leggeva una decodifica stantia di 7D0000
    dalla texcache), poi zeta, poi texcache.
24. **Fragment program: cache per contenuto e compilazione al record.** Il
    titolo tiene i FP in un anello in memoria principale (IO 0x120000..,
    slot da 0x200 → EA 0x21120000..: `MapMainMemory` iniziale IO 0 = EA
    0x21000000; la scrittura e' una `memcpy` per programma, `stvx` per le
    costanti, in `func_004989D8`); una cache per indirizzo restituiva il
    programma del fotogramma prima. Ora: hash FNV del microcodice
    (`fp_ucode_hash`) al momento del draw e compilazione subito
    (`guest_fp_library` nei due `record_draw`), cache chiave hash+vp+ctrl+mrt.
    Misurato con `fp-ora=` nel drawlist: in un fotogramma di 559 draw
    nessun programma cambia fra record e present (il problema era solo la
    cache per indirizzo). Il MSL usa `[[position, invariant]]` +
    `preserveInvariance` (z-prepass EQUAL), `discard_fragment()`.
25. **Present sotto il lock del FIFO** (`cellGcm_fifo_lock/unlock` in
    `boot_main.cpp`): Metal validava "constant buffer offset oltre la
    lunghezza" perche' il present correva insieme al record dei draw.
    `GCM_FLIP_FIFO=1` (in `run.sh`): il flip si completa solo a get==put.

**Perche' il display e' nero anche se la scena c'e'** (la parte lunga,
`run_s27_c211..c250`): il fotogramma finisce con **sei quad neri** (fp
`00121A02`, 1 istruzione `h0=(0,0,0,1)`, `RSX_FP_DUMP=1`): su 03C30000
(512x512), sul **display**, e 4 volte su 016D0000 (viewport pieno, stesso
stato: `RSX_MTRACE=548:562` non mostra nessun cambio di superficie fra un
draw e l'altro). Con lldb (`tools/lldb_guest_bp.py`: breakpoint sui simboli
`func_XXXX`, legge `ctx->gpr` da x0) sono tutte `DrawQuad(id=50)` =
"riempi con un colore" (tabella dei post-shader a `obj+0x65A4+id*4`, obj =
`*(*(0x7CB290)-0x7F9C)` = 0x106F9D70, TOC = 0x7D3190), chiamate da
`func_004B1300..` = il ramo "azzera i buffer" di `func_004AFC68`, preso
quando `(G+0x2E0 / ((G+0x2A0+G+0x2C0)-(G+0x2B0+G+0x2D0))) * G+0x300 == 0`
per i tre canali, con G = render settings del tonemap (statica 0x10512DA0;
`PS3_TTY_DUMP=0x10512DA0+2A0:0x80`). I tre G+0x2E0..0x2E8 valgono 0 perche'
ogni fotogramma `func_00199298` fa memcpy(1.0) e poi `func_00199158`
(blend `dst=(1-w)*dst+w*src`) con src=0 e **w=1.0**: l'override e'
registrato da `func_0051C468(f1=1.0, f2=FLT_MAX)` = **fade a nero**,
chiamato ogni fotogramma da `func_00522458` = lo stato "**Resetting
dive-boat movie-intro: waiting for region N**" (stringhe a 0x10065340..;
`bp5.py` sulla `strcat` `func_00761C50` le ha lette). Cioe': il titolo e'
ancora nel caricamento del checkpoint, e tiene lo schermo nero finche'
tutti i pacchetti delle regioni non sono in stato 5 (`level+0x90`). NON e'
il rendering: i draw neri sono giusti.

**Perche' il caricamento non finisce:** (a) e' lento — ~100 `RequestLevel`
(dive-boat, attori, sky-dive-boat, dive-wide, dive-ocean, temple-0a,
sky-temple...) e ogni pacchetto costa 1-2 fotogrammi di "login" a 2-5
fps (misura `RSX_FRAMESTAT=100`: menu 60 fps, livello 5.5 fps con 367
draw, 2.2 fps con 1080 draw); il profilo (`sample`) dice: main thread PPU
al 61% dentro `func_0074232C` (spin sul job manager, nessuna syscall),
walker 8%, thread SPURS (uno solo, `Sleep(1)` per passata) al 64% in
nanosleep e 18% nel job 215; (b) in ~meta' delle corse **la lettura di
`sky-dive-boat.pak` non completa mai** (OpenAsync emesso, la PreadAsync a
volte nemmeno): i thread FIOS (5,6,8,9,10 media, 7 e 11 scheduler) restano
in `cond_wait` — corsa nel nostro HLE, non ancora trovata (con
`PS3_SCTRACE=1 PS3_SCTRACE_TID=5,..,11` i pacchetti che passano fanno
open→pread→close in 50 ms); (c) a lungo andare l'assert
`joblist.cpp(578) m_jobHeaderCommand == kNoJobYet` (AddJob su uno slot non
libero), poi SIGSEGV: sospetto le occlusion query finte (tutto visibile →
1000 draw/fotogramma e piu' job di quanti la lista ne tenga).

Provato e scartato: `SPURS_FORCE_SPUS=3` (3 SPU virtuali in parallelo):
il boot si ferma a 200 fotogrammi (il job manager WWS non regge lane
concorrenti, come per LBP). `SPURS_IDLE_MAX=0` (niente backoff del kernel
SPURS): il caricamento avanza un po' piu' in fretta (106 pacchetti in 130
s contro 97 in 195 s), fps invariati.

Non-problemi verificati: `Technique index 0 already exists` (dati del pak,
gia' in sessione 27); `Unknown semantic 0x25` / `Lost 1 shader parameter`
(stampe del titolo).

**Stallo FIOS RISOLTO (`run_s27_c257b..c260b`, 4/4 corse caricano
`sky-dive-boat`, prima ~1 su 2).** Con `sample` sul processo fermo
(`tools/stallcatch.sh`): un media thread FIOS era fermo per sempre in
`cellSpursEventFlagWait` (flag 0x2A870C80, bit 1) mentre teneva il mutex 52
(`PS3_MUTEX_DBG=1` lo denuncia dopo 3 s), e l'altro media thread aspettava
quel mutex. Il flag lo mette il **task SPURS edge-zlib** (taskset
0x2A86F380, task 0, segnalato con `SendSignal` per ogni chunk da 1 MB) con
GETLLAR/PUTLLC; il PPU, al risveglio, faceva il clear AUTO come
read-modify-write NON atomica (`current & ~received` scritto con
`vm_write16`): se il PUTLLC del chunk dopo atterrava fra la lettura e la
scrittura, il PPU cancellava il bit appena messo e aspettava per sempre.
Ora il clear (e `TryWait`/`Clear`) rilegge e scrive sotto
`spu_atomic_line_lock` (lo stesso lock del commit del PUTLLC). In piu'
`cellSpursRemoveWorkload` ora dimentica il contesto persistente del PM
(`spurs_pm_forget`): Uncharted rimuove e ri-aggiunge il wid 12 ~700 volte
sempre con la stessa coda, e il kernel vero ricarica il modulo.

### Il prossimo passo (quinta parte)

1. (fatto: stallo FIOS, vedi sopra)
2. Occlusion query vere nel backend Metal (`MTLVisibilityResultMode`) o
   almeno un conteggio realistico: taglia i draw per fotogramma e forse
   l'assert `joblist.cpp(578)`.
3. Velocita': il PPU spinna in `func_0074232C` aspettando il job manager;
   il kernel SPURS dorme 1 ms per passata su un solo thread. Misurare
   quante passate/kick servono per fotogramma prima di toccare la
   pianificazione.
4. Dopo il caricamento completo (regioni in stato 5) il fade dovrebbe
   togliersi da solo: verificare con una corsa lunga senza stallo
   (`SPURS_IDLE_MAX=0`), screenshot a 300+ s.

### Strumenti nuovi (quinta parte)

- Metal: `RSX_DRAWLIST_MIN/AFTER` (il primo fotogramma con ≥n draw dopo il
  fotogramma m; con `RSX_DUMP_FRAME` salva anche le superfici),
  `RSX_DRAW_LIMIT`, `RSX_DEPTH`, `RSX_DEPTH_FUNC`, `RSX_HDR`,
  `RSX_DEBUG_TINT=3` (depth del guest), `RSX_FP_DUMP=1` (MSL con hash),
  drawlist con `fp=addr/hash`, `fp-ora=` se il microcodice e' cambiato al
  present, `tex u:WxH@off/fXX`, `[fmt ct pitch B Z dt sc]`.
- `RSX_MTRACE=<lo>:<hi>` (metodi RSX mentre il contatore dei draw del
  backend e' in [lo,hi], armato da `PS3_TTY_BT`), `RSX_LABEL_DBG=<off>`
  (tutte le scritture di quel label), `[spu-dma] ... (valore 0x..)` nel
  watchdog delle GET identiche, `MapMainMemory -> io=`.
- `PS3_SCTRACE_TID=<tid>[,..]` + timestamp `t=..ms` in `[sc]`.
- `SPURS_IDLE_MAX=<n>` (esponente massimo del backoff del kernel SPURS).
- `PS3_MUTEX_DBG=1` (un `sys_mutex_lock` che aspetta piu' di 3 s stampa
  mutex, proprietario, lr dell'ultimo lock e catena del thread in attesa).
- `tools/stallcatch.sh <log> [VAR=..]`: corsa che aspetta la richiesta
  di `sky-dive-boat`, e se dopo 45 s non e' caricato campiona il processo
  con `sample` (le pile di TUTTI i thread: e' cosi' che si e' visto il
  thread fermo nell'event flag).
- lldb sul processo vivo: `lldb --batch -o "command script import bp.py"
  -o "script bp.run(lldb.debugger, <pid>, <s>)"`; i simboli `func_XXXX`
  esistono (C++ mangled), `ctx` e' in x0, `gpr[n]` a x0+8n, `fpr[n]` a
  x0+256+8n, la memoria guest a `vm_base + ea` (big-endian).

### Il prossimo passo

1. Il rettangolo nero e la scena nel quadrante: `RSX_DRAWLIST=<frame>` su un
   fotogramma con i draw dei job (VP hash/FP/superficie/viewport/scissor) e
   confronto con i draw del post-processing; depth test nel backend Metal.
2. Perche' i draw escono neri: FP dei job (texture non caricate? costanti?).
3. Assert non deterministico `joblist.cpp(578)` sul thread audio (tid 14).

### Il prossimo passo (prima del punto 22, superato)

1. `NV4097_GET_REPORT` nel walker (0x1800: `(type<<24)|offset`, contesto
   0xFEED0001 = IO → EA con `gcm_io2ea`; scrivere {ts,ts,valore,0}, valore
   = 1 per ZPASS finche' non c'e' l'occlusion query) e poi capire perche' i
   report per gli oggetti (0x14410..) non vengono emessi: guardare il record
   a LS 0x1F600 del job 215 (`+0x18` = EA dello slot) e chi lo costruisce
   (217, `[dma-putval]` a `SPU_DMA_PUTVAL=21014410`).
2. Il rettangolo nero: `RSX_DRAWLIST` su un fotogramma con i draw dei job
   (VP/FP/texture/blend), depth test nel backend Metal.
3. Assert non deterministico `joblist.cpp(578)` sul thread audio (tid 14).

### Strumenti nuovi (quarta parte)

- `SPU_JOBTRACE_REGS=1` (r0-r5, r80, r81 a ogni dispatch),
  `SPU_JOBTRACE_MAX=<n>` (tetto, default 1500),
  `SPU_JOBTRACE_DUMP=pc:addr:len[,..]` (max 4; `addr` puo' essere `rN` =
  slot 0 di quel registro): dump di LS al dispatch di quel pc.
- `SPU_DMA_EAWIN_OVL=<id>`, `SPU_DMA_EAWIN_FULL=1` (tutto il PUT, 64
  parole), `SPU_DMA_PUTVAL=<hex>` (i PUT che contengono quella parola),
  `SPU_LS_WRITE_WATCH_MAX=<n>`.
- `GCM_CALLDUMP=<k>` (il k-esimo buffer CALLato al RET, comandi decodificati),
  `GCM_CALLSTAT=1` (ogni 500 RET: quanti buffer con draw, zeri, istogramma
  dei metodi), `GCM_PARKDUMP=1` (l'anello dopo una CALL parcheggiata),
  `GCM_DATAWATCH=lo:hi` [+ `GCM_DATAWATCH_M=<metodo>`] (metodi con argomento
  in quell'intervallo), `GCM_SEMDBG` ora stampa anche 0x0060/0x01A4.
- `PS3_TTY_SCAN=<hex32>[:mask]` (cerca una parola in tutta la memoria guest
  al primo hit), `PS3_TTY_POKE=addr:byte[,..]` (scrive byte a ogni hit),
  `YDKJ_WWATCH_BT` anche per le write32.
- `[spu-ovl] primo uso del modulo N (t=..)`, `[spu-ret] salto:` (i `bi $rN`
  non-ritorno), `[spu-int] pending ... (cXXXX ...)`, BRANCH-TO-0 con
  `ovl=` e `ret_link=`.

### Il prossimo passo (prima della quarta parte, superato)

1. Il job 218: perche' le API get-buffer del PM gli danno 0. Guardare i
   `[wws-res] bufferSetArray read` e i load command del job (`[wws-batch]`)
   nel tratto dopo "'dive-boat': loaded" (corse da 240 s: il job parte a
   ~200 s con `PAD_SCRIPT=45:x`); confrontare con un job che funziona (227,
   audio, stesse API).
2. Lo sfondo 3D del menu (i buchi da 8 parole restano NOP): stesso mondo.
3. Assert non deterministico `joblist.cpp(578)` sul thread audio (tid 14).
4. Backend Metal: manca il depth test (serve appena la scena 3D arriva).

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
