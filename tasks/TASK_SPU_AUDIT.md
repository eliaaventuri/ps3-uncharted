# TASK SPECIFICATION: SPU-HELPERS-AUDIT — semantica degli helper SPU contro l'interprete di RPCS3

## 1. OBIETTIVO
Confrontare OGNI helper `spu_<mnemonico>` definito in `runtime/spu/spu_helpers.h`
(circa 175 funzioni `static inline`) con l'implementazione della stessa
istruzione nell'interprete SPU di RPCS3 (`tasks/ref/SPUInterpreter.cpp`, copia
locale del sorgente ufficiale) e produrre l'elenco delle DIFFERENZE SEMANTICHE:
casi in cui, per qualche input, il nostro helper restituisce un valore diverso
da RPCS3. Sola analisi: nessuna modifica al codice.

## 2. CONTESTO & IPOTESI
* Il port esegue il codice SPU di un gioco (Uncharted) ricompilato in C: il
  lifter emette chiamate a questi helper. Se un helper e' sbagliato anche solo
  per un sottoinsieme di input, la geometria del gioco esce distorta.
* Due bug gia' trovati per darti il livello di attenzione richiesto:
  - `spu_fi(a,b)` fa `b*(2-a*b)` (passo di Newton per il reciproco), ma `fi`
    segue anche `frsqest`: con b = 1/sqrt(a) il risultato e' sbagliato.
    (Questo e' GIA' noto: non riportarlo, ma usa lo stesso rigore.)
  - `cflts/cfltu/csflt/cuflt` avevano il fattore di scala sbagliato (corretti).
* Attenzione a: ordine dei byte/corsie (il register file e' big-endian per
  corsia: vedi le macro `SPU_W`/`SPU_H`/`SPU_B` in cima al file), semantica
  "preferred slot" (corsia 0), maschere di shift/rotate (`& 0x1F`, `& 0x3F`,
  `& 0x0F` byte vs bit, direzione di `rotqmby`/`rotqmbi`/`shlqby`), `shufb`
  (codici speciali 0x80/0xC0/0xE0, ordine degli operandi a/b), `cbd/chd/cwd/cdd`
  (maschere di inserimento con base r1/`imm`), `gb/gbh/gbb`, `avgb/absdb/sumb`,
  `clz/cntb`, `fsmbi/fsm/fsmh/fsmb`, `mpy*` (quali meta' a 16 bit, segno,
  `mpyh` = (a_hi*b_lo)<<16, `mpys` = prodotto >>16), `addx/carry/sfx/bg/bgx`,
  `cg/cgx`, `selb` (ordine: rt = (rc & rb) | (~rc & ra)), `heq/hgt/hlgt`,
  `fcgt/fcmgt/fceq/fcmeq` (i confronti in valore assoluto), `fesd/frds`,
  `dfa/dfm/dfma/dfms/dfnms/dfnma`, `fscrrd/fscrwr`, `frest/frsqest` (valore
  assoluto in frsqest; nessun inf/NaN sulla SPU: saturazione), `csflt` con
  arrotondamento, `cflts` con saturazione, `rotqbybi/rotqmbybi/shlqbybi`
  (contano i byte da bits 3..6), `rotma/rotmah` (shift aritmetico con
  negazione dell'ammontare), `rothm/rotm` (idem, `-count & 0x3F`), `xsbh/xshw/xswd`,
  `orx`, `bisl/biz/binz/bihz/bihnz` (non sono helper: ignorali).

## 3. VINCOLI & REGOLE DI INGAGGIAMENTO
* NON modificare alcun file del repository.
* Non compilare, non lanciare il gioco.
* Leggi `runtime/spu/spu_helpers.h` per intero e, per ogni helper, la funzione
  corrispondente in `tasks/ref/SPUInterpreter.cpp` (cerca `bool spu_interpreter::<MNEMONICO_MAIUSCOLO>(` o `template ... <MNEMONICO>`); se in RPCS3 la
  semantica e' delegata a intrinseci SSE, ragiona sulla definizione
  dell'istruzione (CBEA) e dillo.
* Per ogni differenza fornisci un input concreto (valori delle corsie) e i due
  risultati attesi (nostro vs RPCS3).

## 4. CRITERI DI SUCCESSO
`tasks/RESULT.md` (usa `tasks/RESULT_TEMPLATE.md`) con:
* Stato `SUCCESS`.
* Sezione "DIFFERENZE" : tabella `helper | riga in spu_helpers.h | cosa fa il nostro | cosa fa RPCS3 | input di esempio | gravita' (alta = geometria/matematica, media = interi/maschere, bassa = casi limite inf/NaN)`.
* Sezione "VERIFICATI UGUALI": elenco compatto dei mnemonici controllati e
  risultati uguali (solo i nomi).
* Sezione "NON TROVATI": helper senza corrispondenza in RPCS3 o viceversa.
* Massimo 200 righe in RESULT.md.

## 5. STOP CONDITIONS
* Se `tasks/ref/SPUInterpreter.cpp` manca o e' vuoto: `BLOCKED`.
