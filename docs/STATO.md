# Stato del progetto

Ultimo aggiornamento: **7 settembre 2026** — sessione 2.

## Dove siamo

Fase 2 (far partire un gioco su macOS): **il percorso di boot esegue codice
PowerPC ricompilato su macOS arm64**. Non è un gioco — è un'immagine sintetica —
ma loader, registrazione, HLE, syscall e `ppu_run` hanno eseguito codice guest
fino a un ritorno pulito, ed è il primo caso su questa piattaforma.

| | stato |
|---|---|
| Runtime library arm64 | compila |
| Suite del lifter (9) · SPU (63) · RSX | verdi |
| Metal end-to-end (`ps3recomp_host`) | clear, draw, 60 frame: pixel corretti |
| Ratchet PPU boot scaffold | 0 (era 39) |
| **Boot POSIX end-to-end** | **PASSED** su immagine sintetica |
| Far girare **Uncharted** | no — manca l'EBOOT decifrato |

## Scoperta che cambia il piano

**Uncharted: Drake's Fortune (BCUS98103) è il titolo di riferimento di ps3recomp
fin dall'inizio**, non un obiettivo lontano. Da `docs/PPU_RECOMP.md` e
`docs/SPU_LIFTER.md`:

- **14.104 funzioni** (via tabella OPD), entry `0x10230`, TOC `0x7d3190`;
- **172 import** su 12 librerie, copertura HLE **69%** (118/172), cellGcmSys 100%;
- copertura ISA del lifter PPU: **99,8%** su codice Uncharted reale;
- il primo boot upstream si ferma a **`sys_initialize_tls`** (NID `0x744680A2`),
  la prima chiamata del CRT;
- lato SPU: **1 immagine estratta** dall'EBOOT (39.608 B), **461 funzioni**
  rilevate, **0 gap del lifter**, e la funzione entry esegue fino a un ritorno
  naturale nell'harness;
- il lifter ha già `--trace` per fare il diff contro RPCS3.

Conseguenza pratica: **l'EBOOT decifrato serve ora**, non alla Fase 4. È il
prossimo collo di bottiglia del progetto.

Nota: `docs/PPU_RECOMP.md` dice che il lift completo sarebbe "88 MB di C in un
file" e che lo splitting è il lavoro di scaling principale. **Non è più vero**:
il lifter emette già chunk (`ppu_recomp_000.cpp` …). La documentazione upstream
è indietro rispetto al codice.

## Fatto in sessione 2

1. **`vm.h`: allineamento alla pagina host.** `mprotect` rifiuta intervalli non
   multipli della pagina host (16 KB su Apple Silicon, 4 KB nel codice): ogni
   `vm_commit` guest falliva e la scrittura seguente andava in **SIGBUS** —
   compreso il commit dei PT_LOAD nel loader. Aggiunte `vm_host_page()` /
   `vm_host_range()`; guard page dello stack portata a una pagina host intera.
   Nuovo test `runtime/memory/tests/test_vm_page_align.c` (6/6).
2. **Primo boot POSIX.** `tools/make_test_elf.py` (genera un PPC64 minimo),
   `runtime/ppu/tests/boot_smoke/main.cpp` (il port minimo), e
   `tools/test_boot_posix.py` che lega generazione → lift → compilazione →
   esecuzione. Aggiunto allo workflow macOS.
3. **Link su Mach-O.** `ppu_hle.cpp` chiama l'hook SPURS di flOw con una
   dichiarazione `weak`: ELF risolve a 0 da solo, Mach-O no. Serve
   `-Wl,-U,_flow_spurs_kernel_spawn_postinit`, aggiunto al template dei port.
   (Provato prima `weak_import` sulla dichiarazione: **non basta**, e la modifica
   è stata scartata.)

## Prossimo passo

Il lavoro a valore più alto senza materiali è finito. Da qui servono due cose,
in parallelo:

1. **Da Elia: l'`EBOOT.elf` decifrato di Uncharted.** Sblocca il lift vero,
   il confronto con i numeri upstream e la ripresa del boot da
   `sys_initialize_tls`.
2. **Senza materiali** resta il backend Metal (fragment program del guest e
   texture, testabili con `ps3recomp_host` senza gioco) — è la Fase 3.

## Cosa serve da Elia

- **Ora:** l'EBOOT decifrato (dalla sua copia), oppure il via libera a lavorare
  sul Metal mentre lo procura.
- Prima del lift completo: spazio disco (171 GB liberi oggi).
