# Stato del progetto

Ultimo aggiornamento: **7 settembre 2026** — sessione 1.

## Dove siamo

Fase 2 (far partire un gioco su macOS). Il boot scaffold PPU ora **compila** su
macOS arm64: era il prerequisito, non ancora il traguardo.

| | stato |
|---|---|
| Runtime library su macOS arm64 | compila, `libps3recomp_runtime.a` verificata arm64 |
| Suite del lifter (9 file Python) | 9/9 verdi |
| SPU helper tests | 63/63 |
| RSX texture layout + primitive | verdi |
| Metal end-to-end (`ps3recomp_host`) | clear, draw, 60 frame: pixel corretti |
| **Ratchet PPU boot scaffold** | **0 errori** (era 39) |
| Far girare un gioco ricompilato | **no** — nessuna immagine liftata, e il boot non è mai stato eseguito |

## Fatto in sessione 1

1. Clone di ps3recomp @ `d31bb91` (v0.11.0) come fork privato, remote `upstream`.
2. **Fix build:** `libs/filesystem/cellFs.c` chiamava `RtlCaptureStackBackTrace`
   senza includere `win32_backtrace.h` → la build macOS di `master` era rotta,
   nonostante il README la dia per funzionante. Aggiunto l'include.
3. **Ratchet 39 → 0** in `runtime/ppu/ppu_loader.cpp`:
   - incluso `win32_compat.h` (dava già `LONG`, `SIZE_T`, `_Interlocked*`,
     `YieldProcessor`, e `stdlib.h` per `getenv`);
   - 21 × `__declspec(thread)` → `PPU_THREAD_LOCAL`, il macro che il progetto ha
     già in `ppu_tls.h`;
   - `ps3_guest_ptr_veh` / `ps3_install_guest_ptr_trap` isolate sotto
     `#ifdef _WIN32` con stub POSIX, stesso pattern già usato da `ppu_guard_page`.

## Blocco noto, da affrontare prima di eseguire

**Le pagine da 16 KB.** `runtime/memory/vm.h:86` definisce `VM_PAGE_SIZE` a
**4096**; la pagina reale su Apple Silicon è **16384** (`getconf PAGE_SIZE`).
`vm_init()` regge (le sue regioni sono allineate), ma `vm_commit()` e
`vm_protect()` chiamano `mprotect` arrotondando a 4 KB: qualunque indirizzo o
dimensione non allineato a 16 KB fallirà con `EINVAL` appena il guest alloca
davvero. Va risolto prima di sperare in un boot, e la fix non deve toccare il
comportamento Windows (dove 4 KB è giusto).

## Prossimo passo

Serve un'**immagine liftata** per esercitare il boot su POSIX: senza quella non
si può andare oltre la compilazione. Due strade da valutare nella sessione 2:
1. i port pubblici (`sp00nznet/rubberducky`, `twistedmetal`, `flow`) — verificare
   se pubblicano il C generato o solo gli script;
2. liftare qualcosa di piccolo in proprio.

Poi: allineamento pagine 16 KB, e primo tentativo di boot fino a `main`.

## Cosa serve da Elia

- Nulla per la sessione 2.
- Prima della Fase 4: la sua copia di Uncharted e l'eseguibile decifrato.
- Prima della Fase 4: spazio disco (oggi 171 GB liberi, non bastano per un AAA
  liftato più gli artefatti di build).
