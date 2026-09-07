# Stato del progetto

Ultimo aggiornamento: **7 settembre 2026** — sessione 3.

## Dove siamo

Fase 2 fatta nella sostanza (il boot esegue codice guest su macOS) e **Fase 3
arrivata ai due pezzi che il README upstream dichiara mancanti per Metal**: i
fragment program del guest e le texture. Entrambi ora funzionano.

| | stato |
|---|---|
| Runtime library arm64, suite lifter/SPU/RSX | verdi |
| Ratchet PPU boot scaffold | 0 (era 39) |
| Boot POSIX end-to-end | PASSED (immagine sintetica) |
| Metal: clear, flip, draw | OK |
| **Metal: fragment program del guest** | **OK** (`--fp`) |
| **Metal: texture** | **OK** (`--tex`) |
| **Metal: programma del guest che campiona la texture** | **OK** (`--fp-tex`) |
| Far girare **Uncharted** | no — manca l'EBOOT decifrato |

## Scoperta della sessione 2, ancora la più importante

**Uncharted: Drake's Fortune (BCUS98103) è il titolo di riferimento di
ps3recomp fin dall'inizio**: 14.104 funzioni, 172 import (69% coperti), ISA
lifter al 99,8% su codice Uncharted reale, boot upstream fermo a
`sys_initialize_tls`. Lato SPU: 1 immagine estratta dall'EBOOT, 461 funzioni,
0 gap del lifter. Vedi `docs/PPU_RECOMP.md` e `docs/SPU_LIFTER.md`.

## Fatto in sessione 3

1. **Il decompiler dei fragment program emette anche MSL.**
   `rsx_fp_decompile_lang(..., RSX_FP_LANG_MSL, ...)`. I corpi sono quasi
   identici fra i due dialetti; divergono preambolo, `frac`/`lerp`/`.Sample()`,
   i cast C-style (vietati in MSL) e il ternario a condizione vettoriale
   (sostituito da `select`). Test `libs/video/tests/test_fp_msl.c`: otto
   programmi compilati dal compilatore `metal` di Apple, 8/8.
   **L'HLSL resta byte-identico** — verificato su 256 combinazioni, 7008 righe.
2. **Il backend Metal esegue i programmi del guest.** Una libreria compilata a
   runtime per programma, in cache, con fallback al builtin.
3. **Il backend Metal carica e campiona le texture**, riusando
   `rsx_texture_layout`/`rsx_texture_decode` condivisi con D3D12.
4. **Tre bug trovati e corretti nel test host**: il formato texture andava nei
   bit 8-15 e non nel byte basso; mancava `ppu_vm_size`, quindi i backend non
   avevano un limite per il controllo di validità; l'indirizzo texture va
   risolto come in `rsx_vertex_fetch` (bit 31 = MAIN) e **validato**, perché
   prima uno stato incoerente faceva SIGSEGV dentro `rsx_texture_decode`.

## Cosa manca ancora al backend Metal

- **vertex program del guest** (`rsx_vp_decompiler.c` emette solo HLSL: stesso
  trattamento del fragment, presumibilmente più semplice);
- depth/stencil, MRT, render-to-texture;
- il crossbar `TEXTURE_CONTROL1` e i sampler per unità (oggi un sampler unico
  lineare/repeat per tutte).

## Prossimo passo

1. **Da Elia: l'ISO decrypted di Uncharted.** Da lì si estrae l'EBOOT.BIN e lo
   si decifra (il progetto ha `tools/test_unfself.py`, quindi un unfself c'è).
   Sblocca il lift vero e la ripresa del boot da `sys_initialize_tls`.
2. Senza materiali: il vertex program in MSL, poi depth/stencil.

## Cosa serve da Elia

- **Ora:** l'ISO, quando l'ha sottomano.
- Prima del lift completo: spazio disco (171 GB liberi oggi).
