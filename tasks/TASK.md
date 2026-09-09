# TASK SPECIFICATION: SPU-FLAG-01 — chi scrive il flag a 0x20000000

## 1. OBIETTIVO
Estrarre dal codice SPU liftato la funzione che contiene l'indirizzo `0x06120`
e riportare **testualmente** il codice attorno, più i punti da cui ci si arriva.
Nient'altro. Sola lettura.

## 2. CONTESTO
Il runtime registra questo DMA:

    [dma-eawin] img=2 pc=0x06120 PUT lsa=0x10400 ea=0x20000000 size=0x10

cioè: a `pc=0x06120` la SPU trasferisce 16 byte dalla propria local store
all'indirizzo `0x20000000` della memoria principale. Il PPU aspetta il bit 0 di
quella parola, quindi quel DMA è la fine di un lavoro.

Il file da leggere è **uno solo**:
`/Users/elia/Documents/Coding/uncharted-data/spu_lifted/spu_0000_at_00845000/spu_recomp.c`

Nel codice liftato le funzioni si chiamano `spu_func_XXXXXXXX` (X = esadecimale
dell'indirizzo) e i blocchi hanno etichette del tipo `loc_XXXXX` o commenti con
l'indirizzo. L'indirizzo cercato è `0x6120` / `0x06120` / `6120`.

## 3. VINCOLI
* **NON modificare nessun file.** Sola lettura.
* Non compilare, non lanciare il gioco.
* Non leggere altri file oltre a quello indicato (e alla sua intestazione `.h`
  se serve per capire i nomi).
* Se il file è troppo grande da leggere tutto, usa `grep -n` per trovare le
  righe e poi leggi solo gli intervalli utili.

## 4. CRITERI DI SUCCESSO
`tasks/RESULT.md` contiene, nella sezione "EVIDENZE RACCOLTE":

1. **Nome della funzione** che contiene `0x6120`, e la riga del file dove
   comincia.
2. Le **40 righe** di codice liftato attorno a `0x6120` (copiate testualmente).
3. L'elenco dei **salti che arrivano** a quel blocco: cioè ogni riga del file
   che contiene `6120` come destinazione (`goto loc_...6120`, o simile), con il
   numero di riga.
4. La **condizione immediatamente precedente**: l'`if` o il confronto che decide
   se si arriva a quel blocco oppure no, copiato testualmente.

Se una di queste quattro cose non è nel file, scrivi `NON TROVATO` e la ragione,
invece di indovinare.

## 5. STOP CONDITIONS
* Qualsiasi modifica a un file: fermarsi.
* Se `0x6120` non compare nel file, riportare `BLOCKED` con l'output del grep.
