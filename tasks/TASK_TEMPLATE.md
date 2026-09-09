# TASK SPECIFICATION: [ID_TASK] - [Titolo Breve]

## 1. OBIETTIVO
* Descrivere in 1-3 frasi l'obiettivo preciso dell'operazione assegnata al Worker.

## 2. CONTESTO & IPOTESI
* Contesto rilevante (crash riscontrato, comportamento inatteso, funzione da mappare).
* Ipotesi di lavoro (cosa si suppone causi il problema o come risolverlo).

## 3. VINCOLI & REGOLE DI INGAGGIAMENTO
* File autorizzati alla modifica: `[elenco file]`
* File vietati: `[elenco file]`
* Non superare `[N]` tentativi di modifica senza aver verificato la compilazione.
* Operare in modo strettamente reversibile.

## 4. CRITERI DI SUCCESSO
* Compilazione priva di nuovi warning/errori bloccanti: `cmake --build build`.
* Superamento del test specifico o ratchet del scaffold.
* Assenza di regressioni nei test esistenti.

## 5. STOP CONDITIONS (Condizioni di Arresto Immediato)
* Errore di compilazione non correlato al file modificato.
* Regressione non risolvibile in 2 tentativi.
* Raggiungimento di una decisione di design o semantica non documentata nel task.
