# TASK SPECIFICATION: TONE-01 — perche' il fotogramma 2450 della corsa c62 e' uscito "slavato"

## 1. OBIETTIVO
Confrontare l'elenco dei draw (`[drawlist]`) del fotogramma 2450 in due log di
due corse dello stesso port, `~/Documents/Coding/uncharted-data/run_s33_c62.log`
(immagine SLAVATA: mare celeste chiaro, cielo bianco) e
`~/Documents/Coding/uncharted-data/run_s33_c63.log` (immagine normale, uguale a
tutte le altre corse), e dire COSA differisce nella catena di post-processing.
Sola analisi: nessuna modifica al codice. Risposta in `tasks/RESULT.md`.

## 2. CONTESTO
* Ogni riga `[drawlist] #<n> surf=<superficie> [...] vp=<hash> fp=<addr>/<hash> n=<vertici> ... blend=... sc=<scissor> ... tex: <unita>:<WxH>@<offset>/f<fmt> ...`
  e' un draw del fotogramma; `surf=` e' la superficie di destinazione, `tex:`
  le texture lette. Le righe stanno in blocco dopo `[drawlist] fotogramma #2450`.
* La catena di post-processing di Uncharted, in ordine, e' (superfici):
  scena HDR 016D0000 (1280x720) -> 03120000 (160x90) -> 031E0000 (640x360,
  quadranti) -> 03EB0000/03EC0000/03ED0000/00030000 (log-luminanza fino a 1x1)
  -> 03150000/03180000/031B0000 (bloom 160x90) -> composizione finale sul
  display (surf 00050000 o 00410000, fp A310E8C2 o 00BAFDB5, 6 vertici) ->
  HUD (fp 77331AA8). La composizione legge tex0=016D0000, tex1=320x180@032C1000,
  tex2=160x90@031B0000, tex3=depth 02D50000.
* Il fotogramma 2450 delle due corse ha la stessa scena (barca dei pirati con
  scia bianca all'orizzonte), ma solo c62 e' uscito slavato. Vogliamo sapere se
  in c62 manca un pass, se un pass ha texture/scissor/blend diversi, o se
  l'ordine dei pass e' diverso.

## 3. COSA PRODURRE (in `tasks/RESULT.md`, max 60 righe)
1. Per ciascuna delle superfici della catena (016D0000, 03120000, 031E0000,
   03EB0000, 03EC0000, 03ED0000, 00030000, 03150000, 03180000, 031B0000,
   025D0000, 033C0000, display): numero di draw in c62 e in c63, e per i pass
   da 6 vertici (quad a schermo intero) le differenze in `tex:`, `sc=`, `blend=`.
2. I draw presenti in una corsa e assenti nell'altra fra quelli con n=6 o n=12
   (i pass a schermo intero), con la riga completa.
3. L'ordine delle superfici di destinazione (sequenza dei cambi di `surf=`
   negli ultimi 60 draw) nelle due corse, affiancato.
4. Se c'e' una differenza che spiega "slavato" (esposizione, un pass saltato,
   un bloom scritto due volte), dirlo in 2 frasi; se no, dire "nessuna
   differenza strutturale" e cosa resta da misurare.

## 4. VINCOLI
* Non modificare nessun file tranne `tasks/RESULT.md`.
* I log sono lunghi (116k e 120k righe): estrarre solo le righe `[drawlist]`
  (grep) prima di ragionare.
* Stato finale in cima a RESULT.md: `SUCCESS` (analisi fatta) o
  `NEEDS_OPUS_DECISION` (dati insufficienti, spiegare cosa manca).
