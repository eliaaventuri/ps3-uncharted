# TASK SPECIFICATION: SPU-FLAG-02 — l'ultimo lavoro è stato chiesto o no?

## 1. OBIETTIVO
Stabilire con un numero se, quando il gioco si ferma, l'ultimo lavoro SPU era
stato **chiesto e mai finito**, oppure **mai chiesto**. Sola misura: nessuna
modifica al codice.

## 2. CONTESTO
La stretta di mano è: il PPU azzera la parola a `0x20000000` (arma il flag e
chiede il lavoro), la SPU la riscrive a `0x00000001` quando ha finito, il PPU
aspetta il bit 0. Quindi:

* scritture del PPU di **0** a `0x20000000`  = lavori CHIESTI
* trasferimenti DMA della SPU verso `0x20000000` = lavori FINITI

Se alla fine `chiesti == finiti + 1`, l'ultimo lavoro è stato chiesto e non è
mai tornato. Se `chiesti == finiti`, il PPU aspetta un lavoro che non ha mai
chiesto (e il problema è a monte).

## 3. COMANDO DA ESEGUIRE
Una sola corsa, dalla cartella dei dati, con un tetto di tempo:

```bash
cd ~/Documents/Coding/uncharted-data
YDKJ_WWATCH=0x20000000 YDKJ_WWATCH_LEN=0x4 \
SPU_DMA_EAWIN=0x20000000:0x20000010 \
./run.sh > /tmp/spuflag.log 2>&1 &
BP=$!; sleep 150; kill -9 $BP 2>/dev/null; wait $BP 2>/dev/null
```

## 4. VINCOLI
* **NON modificare nessun file del repository.**
* Non compilare.
* Il log è grande: **non leggerlo tutto**, usa `grep -c` e `grep -n | tail`.
* Se `run.sh` non parte o il log è vuoto, riporta `BLOCKED` con l'errore.

## 5. CRITERI DI SUCCESSO
`tasks/RESULT.md`, sezione "EVIDENZE RACCOLTE", contiene questi sei numeri,
ciascuno con il comando che l'ha prodotto:

1. righe totali del log: `wc -l /tmp/spuflag.log`
2. lavori CHIESTI: `grep -ac 'WWATCH.*0x20000000 = 0x00000000' /tmp/spuflag.log`
3. lavori FINITI: `grep -ac 'dma-eawin.*ea=0x20000000' /tmp/spuflag.log`
4. numero di riga dell'ULTIMO lavoro chiesto
5. numero di riga dell'ULTIMO lavoro finito
6. il gioco si è fermato? `grep -ac 'Rendering is stuck\|HOTREAD' /tmp/spuflag.log`
   e, se sì, le ultime 3 righe distinte del log

## 6. STOP CONDITIONS
* Qualsiasi modifica a un file del repository: fermarsi.
* Se il gioco non parte, `BLOCKED`.
