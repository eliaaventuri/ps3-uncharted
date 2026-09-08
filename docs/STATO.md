# Stato del progetto

Ultimo aggiornamento: **7 settembre 2026** — sessione 4.

## Dove siamo

**Uncharted: Drake's Fortune esegue su macOS.** Non è giocabile: il boot arriva
all'inizializzazione del Job Manager SPU di Naughty Dog e lì crasha. Ma il
percorso completo — ELF decifrato → lift → compilazione nativa ARM64 →
esecuzione — funziona, e il gioco esegue il proprio codice di avvio.

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

## Sessione 15 — dove si ferma adesso, e cosa lo dice

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
