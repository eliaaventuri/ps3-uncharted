# TASK SPECIFICATION: RSX19-SPU-FLAG — la stretta di mano PPU/SPU sul flag a 0x20000000

## 1. OBIETTIVO
Mappare, **senza modificare codice**, il protocollo con cui il PPU e la SPU si
scambiano il "lavoro finito" attraverso la parola a EA `0x20000000`, e dire
esattamente **quale condizione** deve verificarsi perché la SPU scriva quella
parola. Il risultato va in `tasks/RESULT.md`.

## 2. CONTESTO & IPOTESI

Il port è arrivato a girare in continuo (~30 fotogrammi al secondo di comandi
RSX drenati) ma il thread principale del PPU resta fermo per sempre in uno spin.

Fatti misurati (non ipotesi):

- Il PPU gira in `func_00566108` (file generato `recomp/ppu_recomp_003.cpp` in
  `~/Documents/Coding/uncharted-data`), chiamato da `func_005324E8` con
  `lr=0x00532620`. Il ciclo è a `loc_00566120`: legge `r11 = [r3+0x320]`
  (che vale `0x20000000`) e ripete finché il bit 0 della parola a quell'indirizzo
  è zero. 149.240 righe di log consecutive su questo spin.
- La SPU **sa** scrivere quel flag: `img=2 pc=0x06120 PUT lsa=0x10400
  ea=0x20000000 size=0x10`, con dati `00000001 00000000`. Succede ~56 volte e
  poi smette.
- Il task SPU 0 del taskset `0x2A86F380` entra in `WAIT_SIGNAL` esattamente 200
  volte (gira ~5 ms per giro) e alla 200ª resta parcheggiato per sempre.
- `cellSpursSendSignal(taskset=0x2A86F380, id=0)` viene chiamata 463 volte e poi
  smette: chi la chiama è (probabilmente) proprio il thread PPU bloccato.
- Le funzioni PPU `func_00566350` e `func_00566980` scrivono **zero** a
  `0x20000000` (cioè azzerano il flag prima di chiedere il lavoro).

Ipotesi da verificare o smentire: il PPU azzera il flag, chiede il lavoro alla
SPU, e aspetta. La SPU, al 200º giro, invece di fare il lavoro e scrivere il
flag, si mette in attesa di un segnale che solo quel PPU manderebbe.

## 3. VINCOLI & REGOLE DI INGAGGIAMENTO
- **NON modificare nessun file.** Questo è un task di sola lettura e relazione.
- File da leggere (generati, quindi **vietato toccarli**):
  `~/Documents/Coding/uncharted-data/recomp/ppu_recomp_003.cpp`,
  `~/Documents/Coding/uncharted-data/spu_lifted/` e `spu_pm/`.
- File del runtime da leggere: `runtime/spu/spu_workload.c`,
  `runtime/spu/spu_channels.c`, `libs/spurs/cellSpurs.c`.
- Non lanciare il gioco, non compilare.

## 4. CRITERI DI SUCCESSO
`tasks/RESULT.md` risponde a queste quattro domande, ciascuna con il
riferimento al file e alla riga da cui viene la risposta:

1. Che cos'è l'oggetto passato in `r3` a `func_00566108` (quello con il
   puntatore a `+0x320` e i campi `+0x314`, `+0x318`, `+0x31C`)? Chi lo crea?
2. Chi chiama `cellSpursSendSignal` per il taskset `0x2A86F380` — quale funzione
   PPU, e da quale catena di chiamate?
3. Nel codice SPU liftato dell'immagine 2, qual è la funzione che contiene
   `pc=0x06120` (il DMA che scrive il flag), e qual è la condizione che la
   raggiunge invece di andare in `WAIT_SIGNAL`?
4. Elenca le scritture a `0x20000000` (o all'oggetto `+0x320`) fatte dal PPU e
   di' in che ordine avvengono rispetto alla `SendSignal`.

## 5. STOP CONDITIONS
- Se una delle quattro domande non è rispondibile dai file, scrivilo
  esplicitamente invece di indovinare: "NON DETERMINABILE" più il motivo.
- Nessuna modifica al codice, in nessun caso.
