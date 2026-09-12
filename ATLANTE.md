# ATLANTE — mappa del repo (consultare PRIMA di cercare file)

Una riga per cartella o file chiave. Non mappa `build/`, `graphify-out/`,
`.venv/`, `__pycache__/`, binari. I dati del titolo stanno FUORI dal repo, in
`~/Documents/Coding/uncharted-data/` (EBOOT.elf, recomp/, gen/, obj/, run.sh,
build.sh, il port `uncharted`, `runfor.sh <s> <log> [VAR=..]` = corsa a tempo
con `kill -9` finale: il port ignora SIGTERM, senza -9 restano orfani).

```
CLAUDE.md                 regole del fork; docs/STATO.md e' la memoria operativa
docs/STATO.md             stato, misure, fallimenti — leggere a ogni avvio
docs/PPU_RECOMP.md        numeri del lift PPU (entry, TOC, import)
docs/SPU_LIFTER.md        come funziona il lifter SPU
docs/RSX_GRAPHICS.md      backend RSX, FIFO, Metal
docs/DIAGNOSTICS.md       variabili d'ambiente diagnostiche del runtime
docs/*.md                 il resto: architettura, build, moduli, syscall, FAQ

runtime/ppu/ppu_loader.cpp   carica l'ELF, memoria guest (vm_read/write*), watch YDKJ_WWATCH
runtime/ppu/ppu_hle.cpp      dispatcher HLE degli import PPU (NID -> funzione C)
runtime/ppu/ppu_fs.cpp       file system del titolo (/dev_bdvd, /dev_hdd0/1)
runtime/ppu/ppu_sysprx.cpp   sysPrxForUser
runtime/ppu/ppu_tls.h        PPU_THREAD_LOCAL portabile
runtime/ppu/tests/boot_main.cpp  main del port (finestra, ticker present, boot)
runtime/spu/spu_channels.c   canali SPU, MFC (un motore per contesto), atomici GETLLAR/PUTLLC (SPU_ATOM_EA), intercetti PM
runtime/spu/spu_dma.h        motore DMA (mfc_do_transfer), SPU_DMA_EAWIN, SPU_DMA_WATCH
runtime/spu/spu_context.h    stato di una SPU virtuale (LS, gpr, status)
runtime/spu/spu_workload.c   task SPURS (taskset): WAIT_SIGNAL, park, signal
runtime/spu/spurs_policy.c   esegue il policy module (WwsJob) su una SPU virtuale; SPURS_PM_FRESH, SPURS_PM_WATCHDOG
runtime/spu/spurs_job.c      esecuzione di un job WwsJob liftato
runtime/spu/spu_drain.c      dispatch dei salti indiretti, irq (snapshot registri nel contesto), ritorno
runtime/spu/spu_interp.c     interprete SPU di riserva (blocchi non liftati)
runtime/spu/spu_lockstep.c   esecuzione lockstep per i poll GETLLAR/PUTLLC
runtime/syscalls/            lv2: sys_event, sys_cond, sys_mutex, sys_ppu_thread, sys_fs, ...
runtime/memory/vm.h          layout memoria guest
runtime/platform/            win32_compat.h, darwin_compat.h: shim POSIX/Win32
runtime/host/host_posix.c    finestra SDL/Metal, thread di present, pump eventi
runtime/diag/                trace diagnostici

libs/spurs/cellSpurs.c       SPURS HLE: kernel thread (dispatch dei workload), event flag, ReadyCount
libs/spurs/cellSpursLFQueue.c  coda lock-free (push HLE -> SendSignal al task)
libs/spurs/spurs_taskset.c   taskset HLE
libs/spurs/spurs_pm.c        contesto del policy module
libs/sync/cellSyncLFQueue.c  cellSync LFQueue lato PPU (e la riserva di linea condivisa)
libs/video/cellGcmSys.c      FIFO RSX: walker (drain), fence/ref, flip, GCM_STRAND_FIX
libs/video/rsx_metal_backend.m  backend Metal (modalita immediata, render-to-texture, scissor)
libs/video/rsx_texture_layout.c decodifica texture: swizzle, formati, decompressione BC
libs/video/rsx_dispatch.c    decodifica metodi NV40 -> backend
libs/video/rsx_*decompiler.c vertex/fragment program -> MSL
libs/system/, libs/filesystem/, libs/audio/, libs/input/, ...  HLE delle librerie cell*

libs/video/rsx_commands.c    stato RSX + modalita immediata (SET_VERTEX_DATA fra BEGIN/END)
tools/ppu_lifter.py          lift PPU -> C++ (chunk in uncharted-data/recomp)
tools/spu_lifter.py          lift SPU -> C
tools/lift_jobmods.py        lift dei job module WwsJob
tools/extract_spu_images.py  estrae le immagini SPU dall'ELF
tools/gen_hle_nids.py        genera la tabella NID per il port (--all)
tools/check_ppu_scaffold.py  ratchet del boot
tools/spu_disasm.py, ppu_disasm.py  disassemblatori
tools/lldb_guest_bp.py       breakpoint lldb sui func_XXXX del port vivo (r3-r5, f1, catena)
tools/stallcatch.sh          corsa Uncharted che campiona il processo se il caricamento si ferma
tests/                       torture e conformance del runtime
lbp/, wave/                  altri port (riferimento, non toccare)
fw_spu/                      job/prx firmware liftati (audio, sre)
tasks/                       TASK.md / RESULT.md per i worker
run_worker.sh                lancia il worker (--gemini / --local)
```
