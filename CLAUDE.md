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

### Come è messo il worker (riparato il 09/09/2026)

Tre cose erano rotte e ora funzionano; se un giorno smette, si guarda qui.

1. `~/local-ai-worker/opencode.json` dichiarava il provider locale con
   `"npm": "@opencode-ai/plugin"`, che non è un pacchetto di provider: opencode
   rispondeva `Failed to initialize provider: local-mlx`. Ora è
   `@ai-sdk/openai-compatible`.
2. Quel file sta nella cartella del worker, ma opencode legge la configurazione
   dalla **cartella di lavoro** e da `~/.config/opencode/`. Il provider è stato
   copiato anche nella configurazione globale, così `--local` funziona lanciato
   da dentro il repo.
3. `run_worker.sh` è un symlink: `BASH_SOURCE` restava il percorso del link e
   `git_checkpoint.sh` non si trovava. Ora il link viene risolto.
4. **`scripts/tool_shim.py`** (porta 8081) è nuovo. Qwen2.5-Coder scrive le
   chiamate agli strumenti come JSON nudo, mentre `mlx_lm` ne riconosce una solo
   se è avvolta nei tag `<tool_call>`: senza adattatore il modello *stampava* le
   chiamate invece di eseguirle. Lo shim le converte (e gestisce lo streaming).
   Parte e si ferma insieme al server, e la configurazione punta a 8081.

Nota di realtà: il modello locale processa ~90 token al secondo di prompt, e il
prompt di sistema di opencode è ~9.000 token — quindi **un paio di minuti per
turno**. Va bene per mappature e lavori ripetitivi, non per iterazioni rapide.

5. **Limiti alla cache dei prompt** (in `scripts/start_server.sh`). Senza,
   `mlx_lm` tiene una cache KV per ogni ramo di conversazione e non la butta
   mai: misurato `Prompt Cache: 4 sequences, 10.02 GB` sopra ai ~18 GB dei pesi,
   con la memoria della macchina all'80%. Ora `--prompt-cache-size 1` e
   `--prompt-cache-bytes 3221225472` (3 GB): una sola conversazione in cache,
   che basta a riusare il prompt di sistema. Dopo la modifica: 93% di memoria
   libera. Si possono cambiare con le variabili `PROMPT_CACHE_SIZE` e
   `PROMPT_CACHE_BYTES`.

**Regola operativa: non lanciare mai il gioco mentre il worker locale gira.**
Il modello da 32B e il backend Metal del port si contendono la stessa GPU
unificata, e il server MLX muore con
`[METAL] Command buffer execution failed: Insufficient Memory`. Misurato il
09/09/2026: server crashato a metà task. O si delega la lettura di codice al
worker, o si misura il gioco — mai insieme.

### Flusso Operativo Obbligatorio

1. **Definisci l'obiettivo**: Scrivi un task circoscritto in `tasks/TASK.md` (ispirandoti a `tasks/TASK_TEMPLATE.md`).
2. **Delega l'esecuzione**: Lancia `./run_worker.sh --gemini` o `./run_worker.sh --local`.
   *(Il worker crea in automatico un checkpoint Git di sicurezza e opera in modo reversibile).*
3. **Leggi solo il responso sintetico**: Leggi **esclusivamente** `tasks/RESULT.md`.
   - Se `SUCCESS`: accetta il lavoro e passa all'obiettivo successivo.
   - Se `REGRESSION`: il worker ha già lo stato precedente ripristinato; correggi l'ipotesi e riprova.
   - Se `NEEDS_OPUS_DECISION`: intervieni per sbloccare la decisione di alto livello.
