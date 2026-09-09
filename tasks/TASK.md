# TASK SPECIFICATION: SPU-FLAG-03 — cosa fa la SPU dopo l'ultimo lavoro finito

## 1. OBIETTIVO
Dire, con righe di log copiate testualmente, **che cosa succede sul lato SPU**
fra l'ultimo lavoro completato e la fine del log. Sola lettura di un file di
log già prodotto: **non lanciare il gioco, non compilare, non modificare nulla.**

## 2. CONTESTO
Il file è `/tmp/stallo.log` (~127.000 righe). Fatti già stabiliti:

* Il PPU "arma" un lavoro azzerando la parola a `0x20000000`; la SPU la
  riscrive a `0x00000001` quando ha finito.
* In questa corsa: **114 armamenti** e **57 completamenti** (due armamenti per
  lavoro, è normale).
* L'ultimo **completamento** è alla riga **12980**.
* L'ultimo **armamento** è alla riga **12999**, cioè dopo: l'ultimo lavoro è
  stato chiesto e non è mai tornato.
* Dalla riga ~13000 in poi il PPU gira a vuoto: 107.615 righe `[HOTREAD]
  spinning on 0x20000000`.

## 3. VINCOLI
* **Nessuna modifica a nessun file.** Nessuna compilazione. Non lanciare il gioco.
* Il log è enorme: usa `grep -n`, `sed -n 'A,Bp'`, `grep -c`. **Non leggerlo tutto.**
* Ignora completamente le righe `[HOTREAD]`, `[WAIT]` e `[SIGNAL]`: sono rumore.

## 4. CRITERI DI SUCCESSO
`tasks/RESULT.md`, sezione "EVIDENZE RACCOLTE", contiene:

1. **Le 40 righe** del log fra 12960 e 13060 **escluse** `[HOTREAD]`, `[WAIT]`,
   `[SIGNAL]` — copiate testualmente. Comando suggerito:
   `sed -n '12960,13060p' /tmp/stallo.log | grep -avE '^\[HOTREAD|^\[WAIT|^\[SIGNAL'`
2. **Il conteggio, dopo la riga 13000**, di ciascuno di questi marcatori (uno
   `grep -c` per ciascuno, riportando il numero anche se è zero):
   `spu-park`, `WAIT_SIGNAL#`, `SendSignal`, `wws-cmd`, `spu-ovl`, `spu-assert`,
   `mfc-list`, `dma-eawin`, `cellSpurs`
   Comando: `tail -n +13000 /tmp/stallo.log | grep -ac '<marcatore>'`
3. Le **ultime 20 righe distinte** del log escluse `[HOTREAD]`, `[WAIT]`,
   `[SIGNAL]`, copiate testualmente. Comando:
   `grep -avE '^\[HOTREAD|^\[WAIT|^\[SIGNAL' /tmp/stallo.log | tail -20`
4. Una frase secca: **dopo la riga 13000 la SPU fa ancora qualcosa, sì o no?**

## 5. STOP CONDITIONS
* Se `/tmp/stallo.log` non esiste o è vuoto: `BLOCKED`.
* Qualsiasi modifica a un file del repository: fermarsi.
