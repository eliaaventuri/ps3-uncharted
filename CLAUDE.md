# ps3-uncharted — fork privato di ps3recomp

Obiettivo: **Uncharted: Drake's Fortune (PS3) come eseguibile ARM64 nativo su
macOS**, via ricompilazione statica. Fork privato di
[sp00nznet/ps3recomp](https://github.com/sp00nznet/ps3recomp) (licenza MIT, quindi
nessun obbligo di ridistribuzione). Proprietario: Elia Venturi.

Bersaglio intermedio: la **demo** di Uncharted (NPUA98103) — stesso motore, un
decimo dei contenuti, ed è l'unica cosa Uncharted classificata "Playable" su RPCS3.

## Prima di ogni sessione

Leggi `docs/STATO.md`. Dice dove siamo, cosa ha fallito e qual è il prossimo passo.
Aggiornalo alla fine di ogni sessione, **compresi i fallimenti**: sono la parte che
si perde e che costa di più ritrovare.

## Regole di lavoro

1. **Modifiche chirurgiche.** Questo è un fork di un progetto vivo: ogni riga che
   tocchiamo in più è un conflitto quando ci risincronizziamo con l'upstream.
2. **Segui i pattern già presenti.** Prima di inventare, cerca: il progetto ha già
   `win32_compat.h`, `win32_backtrace.h`, `PPU_THREAD_LOCAL` in `ppu_tls.h`,
   `darwin_compat.h`. Quasi sempre la soluzione giusta esiste già altrove nel tree.
3. **Non rompere Windows.** È l'unica piattaforma dove i giochi girano davvero, ed
   è il nostro riferimento. Ogni fix POSIX va dietro `#ifdef`, mai al posto del
   ramo Win32.
4. **Ogni fase produce un numero.** Se non c'è un numero misurato, la fase non è
   finita.
5. **RPCS3 è l'oracolo.** Quando serve sapere cosa dovrebbe fare il codice guest,
   la risposta si legge lì, non si indovina.

## Comandi

```bash
export PATH="/opt/homebrew/bin:$PATH"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build                                  # build
.venv/bin/python tools/check_ppu_scaffold.py         # ratchet del boot scaffold
PS3RECOMP_METAL_HEADLESS=1 ./build/ps3recomp_host --draw   # Metal end-to-end
```

La CI completa che facciamo girare in locale è `.github/workflows/macos.yml`:
è la definizione operativa di "verde su macOS".

## Git

- `upstream` = sp00nznet/ps3recomp. Risincronizzarsi periodicamente: il progetto
  committa spesso e i nostri file si sporcano in fretta.
- Il nostro lavoro sta sul branch `macos`.


## Politica di Delega Aggressiva ("Delegation-First" per Massimo Risparmio Token)

Il ruolo di Opus è di **Senior Architect / Orchestratore**: i tuoi token devono essere preservati esclusivamente per la visione strategica, l'architettura e le decisioni semantiche.
È **vietato sprecare token di Opus** in attività che i worker subordinati possono svolgere:
* NON fare grep estese o ricerche massive attraverso l'intero albero del repository.
* NON leggere per intero file di log lunghi, dump di memoria o trace Metal.
* NON fare cicli iterativi di compilazione per tentativi ed errori nel contesto di Opus.
* NON eseguire test o verifiche ripetitive se delegabili.

### Matrice di Delega (Chi fa cosa)

1. **Gemini 3.6 Flash (`./run_worker.sh --gemini tasks/TASK.md`)** [Cloud Ultra-Veloce]:
   - Esplorazione codice, grep su larga scala, ricerca pattern in guest/host.
   - Analisi di grandi log di errore, crash dump, disassemblati e trace.
   - Mappatura preliminare di funzioni e dipendenze.

2. **Qwen 32B Locale (`./run_worker.sh --local tasks/TASK.md`)** [Offline Costo Zero]:
   - Cicli continui di compilazione `cmake --build build`.
   - Modifiche chirurgiche a stub, header o funzioni circoscritte.
   - Esecuzione continua di `.venv/bin/python tools/check_ppu_scaffold.py` fino al verde.
   - Ogni operazione ripetitiva che altrimenti consumerebbe token o chiamate API.

### Flusso Operativo Obbligatorio

1. **Definisci l'obiettivo**: Scrivi un task circoscritto in `tasks/TASK.md` (ispirandoti a `tasks/TASK_TEMPLATE.md`).
2. **Delega l'esecuzione**: Lancia `./run_worker.sh --gemini` o `./run_worker.sh --local`.
   *(Il worker crea in automatico un checkpoint Git di sicurezza e opera in modo reversibile).*
3. **Leggi solo il responso sintetico**: Leggi **esclusivamente** `tasks/RESULT.md`.
   - Se `SUCCESS`: accetta il lavoro e passa all'obiettivo successivo.
   - Se `REGRESSION`: il worker ha già lo stato precedente ripristinato; correggi l'ipotesi e riprova.
   - Se `NEEDS_OPUS_DECISION`: intervieni per sbloccare la decisione di alto livello.
