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

## I prossimi tre blocchi, in ordine

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
PS3_VFS_ROOT=/tmp/uc_iso ./uncharted ~/Documents/Coding/uncharted-data/EBOOT.elf
```

La tabella NID è **per-port**: `ppu_hle_register_all()` nel runtime è weak e
vuota, e senza `tools/gen_hle_nids.py --all` ogni import resta non risolto.
