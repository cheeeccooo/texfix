# Texfix nuovo: specifica

Questa specifica dice **che cosa** la nuova app deve fare, non come. È ricavata dalla versione attuale
(`../texfix`, commit `954fb52`), dal suo `CLAUDE.md`, dai suoi 136 commit e dalle intestazioni del motore. La vecchia
app resta il riferimento quando un dettaglio qui manca: si legge il suo codice, non lo si copia.

## Come leggerla

Ogni voce ha un livello:

| Segno | Livello | Che cosa vuol dire |
|---|---|---|
| **[S]** | Scopo | Ciò che l'utente ottiene. Vincolante: la nuova app lo deve dare, anche in un altro modo. |
| **[V]** | Vincolo | Formati e contenuti che esistono fuori dall'app (progetti su disco, `regole/`, `styles`). Vincolante salvo decisione dell'utente. |
| **[O]** | Oggi | Come lo fa la versione attuale. **Non vincolante**: serve a capire. Spesso è un'eredità delle skill e va ripensato. |
| **[C]** | Caso limite | Un problema già incontrato e risolto. Vale **solo se** la nuova app ha ancora la condizione scritta accanto; altrimenti si scarta. |

Ciò che in `[O]` è marcato *→ probabilmente sparisce* è un'ipotesi: la decisione si prende nell'architettura.

---

## 1. Che cos'è Texfix

**[S]** Un programma per macOS che trasforma appunti di ogni tipo in appunti LaTeX da studiare, con uno stile
predefinito semplice o con uno stile importato (come `styles`, §4.2). Le fonti possono essere scansioni a mano, PDF di slide, documenti o `.tex` di altri autori. Un modello
(Claude) trascrive, fonde, controlla e impagina. L'utente guarda, sceglie e corregge in un editor con il PDF accanto.

**[S]** Principi che valgono ovunque:
- **Al centro le lezioni**, non i file. Un corso è una sequenza di lezioni (numero, data, titolo), ognuna con le sue fonti.
- **Un flusso che va avanti da solo.** L'utente non mette in ordine passi tecnici: aggiunge materiale, e ciò che resta
  da decidere gli arriva come elenco di scelte.
- **Ogni decisione è salvata subito**, in un posto solo. Niente sessioni da salvare o chiudere, niente «esci senza salvare».
- **Un solo modo per tornare indietro**: ⌘Z sulla cronologia del progetto, qualunque cosa sia successa.
- **Il modello serve solo dove serve.** Ciò che si decide con un'occhiata lo decide l'utente con un clic; ciò che è
  deterministico lo fa il programma.
- **Costi visibili**: prima di un lavoro che usa il modello si vede la stima del costo; dopo, quanto è costato.
- **Niente fallisce in silenzio.** Ciò che non riesce (il modello non risponde, un file non si legge, un programma esterno
  manca) lo si dice dove l'utente guarda, con il motivo e un modo per riprovare; mai un lavoro che finisce senza dire niente.
- **Parole dell'utente**, in italiano. Nei testi visibili mai: seed, hbox, overfull, profilo, ramo, root, GUI, report
  (si dice esito), sessione, id, percorsi interni.

---

## 2. Il progetto su disco **[O]**

**Decisione D1: la nuova app non apre i progetti esistenti**, che restano alla vecchia app. Il formato qui sotto **non
vincola**: la nuova app definisce il suo, il più semplice che serve agli scopi. Resta utile come elenco di ciò che va
salvato. Restano vincolanti solo i `.tex` prodotti (le convenzioni di `regole/` e `styles`) e il fatto che un progetto
sia una cartella leggibile anche senza Texfix (`main.tex` compila con latexmk).

Il formato della vecchia app (dal 10/11/2026); il modulo indicato è dove lo legge e lo scrive.

| Percorso | Che cosa | Modulo di oggi |
|---|---|---|
| `main.tex` | il documento, sempre con questo nome; nel main solo `\input` delle lezioni e, sotto `\usepackage{styles}`, i pacchetti dichiarati con `% package: nome` | `Project`, `Generated` |
| `styles.sty` | copia del pacchetto, **in sola lettura** | `:/app/styles.sty` |
| `generated.tex` | ciò che il programma aggiunge al preambolo, in blocchi con nome: `comandi`, `lezione`, `tag-provenienza`, `segnaposto-figure` | `Generated` |
| `.latexmkrc` | creato se manca; quello dell'utente non si tocca | `Project::ensureLatexmkrc` |
| `src/srcN.tex` | una lezione (o un argomento, in modo argomenti) | |
| `imgs/` | le immagini usate dalle lezioni | |
| `fonti/coda/` | fonti da convertire | `Sources` |
| `fonti/usate/lezione N/`, `più lezioni/`, `precedenti/` | fonti già usate, per lezione; versioni vecchie con la data | `Sources` |
| `docs/` | documenti di consultazione dell'utente: l'agente li legge, nessuno li cambia | |
| `salvataggi/corso.json` | impostazioni del progetto (vedi §3.15) | `Course` |
| `salvataggi/regole-del-corso.md` | regole del progetto, una per riga «- …» | `Course` |
| `salvataggi/STATO.md` | tabella delle lezioni fra `<!-- >>> lezioni … -->` e `<!-- <<< lezioni -->`, riga `Modo:` (lezioni o argomenti) | `Lessons` |
| `salvataggi/lezioni/<srcN>.json` | stato di una lezione, fonti originali | `LessonState`, `NewLesson` |
| `salvataggi/lezioni/nuove/<srcN>.json` | lezioni create e non ancora convertite | `NewLesson` |
| `salvataggi/fatte.json` | lezioni «fatte»: file → quando | `LessonState` |
| `salvataggi/conversioni/` | esiti delle conversioni con i dubbi; `prima/<sha12(id)>.tex` per le correzioni da confermare | `PipeCtx::REPORTS`, `Review` |
| `salvataggi/tenute.json`, `tolte.json` | aggiunte e correzioni tenute (per hash del testo) o tolte (testo, rientro, riga precedente) | `Review` |
| `salvataggi/figure/` | registro delle figure: `indice.json` (versione 2), `figure.json`, `inserite.json`, `bozze/`, `backup/` | `ImgCrop` |
| `salvataggi/impaginazione/registro` (+ `originali/`, `inizi/`) | registro dell'impaginazione delle box | `BoxFixSession` |
| `salvataggi/versioni.json` | fonti sostituite da una versione nuova | `Sources` |
| `salvataggi/lavori/<id>/` | lavori aperti: `lavoro.json`, `piano.json`, `esito.md` | `Jobs` |
| `salvataggi/esiti/<AAAA-MM-GG HH.MM> — <titolo>.md` | archivio dei piani eseguiti | `Jobs` |
| `salvataggi/chat/<id>.json` | chat con allegati; fuori da git | `ChatStore` |
| `salvataggi/cronologia/` | cronologia di ⌘Z; vale una sessione, fuori da git | `History` |
| `build/` | tutto ciò che si rifà; `build/main.pdf` | |

**[O]** Formati dentro i `.tex` della vecchia app (la nuova può tenerli o sceglierne di migliori):
- marcatori dei blocchi `% @L6.3 · appunti.pdf p.2` (§3.18);
- `\lezione{N}{data}`;
- `% resolvebox:` scritto dall'impaginazione;
- `% package: nome`;
- `% [figura della fonte N]` nelle fonti `.tex`.

**[O]** Formati di prima che la vecchia app migra da sola, sparsi in più punti: `docs/to_be_converted` e
`converted_docs`, `to_distill`, salvataggi in `docs/` o `build/`, seed delle figure (una cartella per conversione e
un archivio globale), blocchi `convert:…` di `generated.tex`, marcatori di `lessons.py` in STATO.md, il main con un altro
nome, `fatta` dentro `salvataggi/lezioni/<srcN>.json`, dubbi come stringhe «P1: … — letto X».
*→ Sparisce (D1): nessuna migrazione, nessun caso limite legato ai formati di prima.*

---

## 3. Funzioni

### 3.1 Progetti
- **[S]** Il foglio dell'inizio, uno solo per «Nuovo progetto» e per «Apri cartella» su una cartella che non è un progetto: per
  data o per argomento (§3.5, con il programma del corso), il profilo, lo stile e, se la cartella ha dei file, dove va
  ognuno (decisione del 23/11/2026).
- **[S]** Nuovo progetto: nasce intero, con `styles.sty`, `main.tex`, le cartelle, `generated.tex`, `.latexmkrc` e il repository git.
- **[S]** Una cartella con dei file diventa un progetto con il foglio dell'inizio: di ogni file (o di una cartella intera) si
  sceglie «In arrivo» (il materiale da smistare: proposto per ciò che Texfix sa leggere), «Da consultare» (in `docs/`, con
  le sue cartelle: proposto per il resto) o «Dov'è». Un nome già preso diventa «nome (2)», i file nascosti restano dove
  sono, le cartelle rimaste vuote si tolgono; non parte niente finché non si preme «Smista». Un progetto senza `main.tex` si completa da sé.
- **[S]** Pagina di benvenuto con i progetti recenti.
- **[S]** Un'istanza sola. Un secondo avvio passa la cartella a quella già aperta.
- **[S]** Le lezioni sono una regola sola: gli `\input{src/…}` non commentati dopo `\begin{document}` di `main.tex`, in ordine. Quelle che mancano su disco si saltano.
- **[C]** *(se si leggono gli input dal main)* Vanno gestiti: un secondo documento nella cartella, un `\input` nel preambolo, uno commentato, uno che manca, l'ultima riga senza a capo. Prima c'erano quattro ricerche del main diverse e in disaccordo. Test: caso `main`.

### 3.2 Editor, PDF, compilazione
- **[S]** Editor LaTeX con evidenziazione. Begin ed end delle box sono scritti nel colore della box nel PDF, letto dallo stile del progetto (§4.2). *[O] Oggi eqbox e genericbox sono in grigio chiaro.*
- **[S]** Il PDF sta accanto all'editor. PDF e immagini si aprono in schede. Un divisore con tre posizioni (PDF pieno, alla pari, editor pieno); doppio clic: alla pari.
- **[S]** Salto sorgente ↔ PDF: ⌘J o ⌘-clic su una riga porta al PDF, compilando prima se serve; ⌘-clic o doppio clic sul PDF porta al sorgente. Anche i collegamenti del PDF funzionano.
- **[S]** Compila il file (⌘B): una lezione da sola, nel main ridotto. Compila tutto (⇧⌘B): il documento in `build/main.pdf`. Avvisi con la riga, avanzamento visibile.
- **[S]** «Risolvi tutto» negli avvisi quando ci sono errori o righe fuori misura (§3.10).
- **[S]** Editor: ⌥ non scrive caratteri speciali; ⌥Z attiva o toglie l'a capo; sui numeri di riga si scorre e un clic porta alla riga.
- **[S]** Menu contestuale sui file: apri, apri e compila, apri con l'app predefinita, mostra nel Finder, copia percorso o nome, rinomina (il main segue la lezione rinominata), duplica, sposta nel Cestino.
- **[O]** latexmk con il `.latexmkrc` del progetto e opzioni fisse sulla riga di comando (`-cd- -file-line-error -synctex=1 -jobname -outdir`); avvisi dal `.log` dell'ultima passata; avanzamento da «Run number N». I lavori interni (prove di impaginazione, figure TikZ, ritagli) usano `pdflatex` diretto.
- **[C]** *(se latexmk gira in un processo figlio)* Qt può non vedere la fine di latexmk (zombie non raccolto). Serve una sorveglianza che chiude la compilazione se il processo non c'è più. Alla chiusura dell'app: niente `waitForFinished` senza limite, segnali bloccati prima di smontare la finestra (c'era un use-after-free).
- **[C]** *(idem)* «Ferma» deve chiudere tutta la famiglia del processo (pdflatex è figlio di latexmk), anche se è in pausa, e aspettare che sia raccolto. Altrimenti la barra resta al 98% «in corso».
- **[C]** *(se si compila con latexmk)* Una compilazione interrotta può lasciare `build/main.aux` troncato: l'indice resta vuoto per sempre. All'annullamento si buttano `.aux` e `.fdb_latexmk`; se un `.aux` rovinato ferma la compilazione, lo si butta e si ricompila una volta.
- **[C]** ▶ File e ▶▶ Tutto sono spenti mentre si compila.

### 3.3 Albero del progetto e stato delle lezioni
- **[S]** Gruppi dell'albero: Lavori; Lezioni (o Argomenti); In arrivo (§3.4); Documento (main, `generated.tex`, file fuori dal main); Fonti senza lezione; Da consultare (`docs/`, ci si trascinano file); Esiti; Immagini (chiuso, divise per il file che le usa).
- **[S]** Una lezione è una riga: pallino dello stato, numero, titolo, data. Il pallino è verde (pronta, con ✓ se fatta), giallo (da rivedere), rosso (errori) o grigio (mai compilata). Dentro ci sono le righe Dubbi, Correzioni, Impaginazione, Figure con il loro conto (un clic apre il pannello su quella lezione) e il ramo Fonti.
- **[S]** Il nome sta a sinistra; a destra, piccolo, il dettaglio (data, conto, stato). Ogni voce ha il nome intero nel suggerimento.
- **[S]** Un clic apre ciò che va in una scheda; il doppio clic apre anche ciò che prende la finestra o un'altra app; la freccia apre solo il ramo. Ciò che l'utente apre o chiude resta così fra un aggiornamento e l'altro.
- **[S]** Lo stato di una lezione mette insieme `\lezione`, le fonti, i dubbi aperti, le aggiunte da tenere, le figure da scegliere o inserire e gli avvisi dell'ultima compilazione. Il documento intero aggiorna tutte le lezioni, un file solo aggiorna la sua.
- **[C]** *(se le fonti si ritrovano per nome)* «notes_LabSS (1).pdf» deve ritrovare notes_LabSS_1.pdf: si confronta il nome senza maiuscole, punteggiatura e « (2)». Questa regola sta in un posto solo (`Sources::bare`).
- **[C]** Le lezioni di prima senza manifest si riconoscono dal nome `srcN.*` o dalla data nel nome dei file in `fonti/usate`.

### 3.4 Aggiungi materiale (decisioni del 18/11/2026)
Una porta sola per il materiale nuovo. Non ci sono più Nuova lezione, il tasto Converti, «Smista in lezioni» né
«Fai il piano»: c'è **Aggiungi materiale**, e il resto lo decide Texfix con un piano che l'utente approva. Il materiale
si smista **per concetti**, non per date o per file: un concetto (una definizione, una derivazione, un circuito con la
sua analisi, un esempio, un esercizio) va dove serve, anche se il file da cui viene ne porta altri altrove.

- **[S]** **Il materiale entra in qualunque modo**: il tasto, file e cartelle trascinati sul pannello (di una cartella,
  con le sue sottocartelle, ciò che Texfix sa leggere), «Aggiungi materiale a questa
  lezione…» dal menu di una lezione (i concetti vanno lì, se uno non sta chiaramente altrove), «Carica una versione
  nuova…» sul menu di una fonte usata. Il materiale **va solo in arrivo**: lo riconosce **«Smista»**,
  accanto ad Aggiungi materiale, quando l'utente vuole, e ne esce **sempre un piano** (decisione del 23/11/2026; prima
  si riconosceva a ogni aggiunta). La lezione scelta con «Aggiungi materiale a questa lezione…» resta del file finché è in
  arrivo.
- **[S]** **Senza modello**, ciò che è meccanico e sicuro: un doppione di una fonte usata (si toglie), una *versione nuova*
  di una fonte usata («Porta le differenze», «Smistala come materiale nuovo», «Salta»), un file che non si legge (resta
  in arrivo con il perché), le lezioni di un `.tex` che il corso contiene tali e quali (marcatori dei blocchi, o testo).
- **[S]** **Con il modello, che legge tutto**: i concetti di ogni lezione del corso (una volta, finché la lezione non
  cambia) e di ogni fonte nuova (di un `.tex` il testo senza preambolo: lo stile dell'autore non conta, solo il
  contenuto), poi la **mappa**: quali concetti compaiono in più punti (fra il materiale nuovo e il corso, o fra due fonti
  nuove) e che cosa li distingue, dove va ogni concetto nuovo (in una lezione che c'è, o in una lezione nuova che può
  prendere concetti da più file), che cosa saltare. Qualità prima del costo: la mappa la fa il modello più forte.
- **[S]** **Il piano**, da leggere a colpo d'occhio, senza sigle:
  1. *Dove il materiale si interseca*: un grafo (a sinistra il materiale nuovo, a destra le lezioni che hai, archi con
     quanti concetti hanno in comune; un clic su un nome mostra solo i suoi), poi un concetto in comune per riga, con dove
     compare, che cosa li distingue e la decisione proposta: «Una versione sola, nella lezione N», «Tienile tutte»,
     «Salta il nuovo»;
  2. *Ciò che resta di questi file*: le lezioni nuove e le aggiunte, con i loro concetti (quelli in comune sbiaditi);
  3. *Tutto nuovo*: ciò che non si interseca con niente.
  In fondo: versioni nuove, già nel corso, parti da saltare, impostazioni del progetto con «Cambia», costo stimato e
  costo della mappa. Una destinazione si toglie con la spunta (i suoi pezzi restano in arrivo) o si corregge con
  un'istruzione; un'istruzione o un commento fanno «Rivedi piano»: si rifà solo la mappa.
- **[S]** **Date.** La data è un indizio fra gli altri. Ciò che non ha data in un progetto per data diventa una lezione
  senza data, in fondo.
- **[S]** **«Avvia»**: di ogni fonte si prendono le righe (o le pagine) dei concetti che vanno in un posto, allargate al
  paragrafo, e diventano un file fra quelli in arrivo. Una lezione nuova di soli testi e `.tex` (anche da più file, con le
  macro di tutti) la scrive la conversione (§3.6); ciò che va in una lezione che c'è, una versione sola di un concetto in
  comune che sta nel corso, una lezione nuova che mescola pagine e testi o che ha un'istruzione la fa l'agente, senza
  chat e senza un piano suo: basta l'approvazione del piano dei concetti (decisione del 18/11/2026). Le aggiunte alla
  stessa lezione sono un compito solo. Gli originali smistati vanno in `fonti/usate/smistate/`. Tutto il lavoro è un
  passo solo della cronologia.
- **[S]** **Il lavoro di «Avvia»** (decisioni del 18/11/2026): Texfix fa da orchestratore, in un lavoro solo, e lo mostra
  in una finestra sua che prende tutto lo spazio come le revisioni: lo stato, quanto è fatto, il tempo, il costo; i
  cantieri (ogni lezione da scrivere con le sue fasi, una fase saltata con il perché, ogni lezione del corso da completare;
  di ognuno che cosa fa ora, o che aspetta il suo turno); le chiamate ai sottoagenti, con che cosa hanno ricevuto, fatto e
  risposto, e insieme a loro i passi senza modello (compilazioni con l'esito, prove di errori e righe e d'impaginazione, la
  preparazione: niente di ciò che il lavoro fa resta nascosto; decisione del 19/11/2026); alla fine l'esito, con «Chiudi e archivia» (i registri delle chiamate si
  buttano) e «Annulla tutto il lavoro» (il progetto torna com'era prima di «Avvia», il piano da avviare).
- **[S]** **Il limite di utilizzo non è un errore.** Quando una chiamata lo trova, il lavoro finisce ciò che è in corso,
  non fa partire niente di nuovo e si ferma da sé, come se il programma fosse stato chiuso e riaperto; dice quando si
  può riprendere («alle 02:21», «domani alle 02:21», «il 21/11 alle 09:00», letto dal messaggio; se non si legge, fra
  mezz'ora) e si riprende con un clic. Ciò che è fatto resta, anche una trascrizione non ancora verificata; l'agente
  riprende la sua conversazione. Sotto la barra: il tempo lavorato (senza le soste) e quanto manca, dal ritmo fin qui
  (decisioni del 19/11/2026).
- **[S]** **Nemmeno la connessione che salta.** Una chiamata (o l'agente) che non raggiunge il modello per la rete lo
  riconosce dal messaggio o provando a raggiungere il servizio; aspetta lì che la connessione torni, guardando ogni 15
  secondi, e si rifà senza contarla fra i tentativi (l'agente continua la sua conversazione). Intanto la barra, le
  Attività e la finestra del lavoro dicono «manca la connessione». Se dopo dieci minuti non è tornata, il lavoro si ferma
  come per il limite, ma si può riprendere subito; un lavoro senza ripresa (Risolvi errori, lo smistamento) si ferma
  dicendolo, e ciò che è fatto resta. La chat dice che il messaggio non è stato eseguito (decisione del 19/11/2026).

- **[S]** **In arrivo** (era «Da convertire»): il materiale aspetta in `fonti/coda/` finché un lavoro non lo porta fra le
  fonti usate. Nell'albero c'è solo se c'è qualcosa; un clic apre il file nell'editor, e ogni file dice il suo stato:
  *da smistare* (il conto è sul tasto «Smista»), *nel piano* (il piano è sotto Lavori), *in lavorazione*, *non
  riconosciuto* con il motivo e «Riprova». Aprire un progetto non riconosce niente da sé.
- **[S]** **Niente fallisce in silenzio.** Se il modello non risponde (connessione, limite d'uso, errore) il materiale resta
  in arrivo con il motivo scritto accanto e «Riprova»; il messaggio va anche nella barra e in Console. Se la mappa non
  torna (un concetto senza posto) si chiede di nuovo dicendo che cosa manca, poi lo si dice.
- **[S]** Se numero e data letti da una scansione non tornano con quelli del piano, si crea un dubbio sulla riga
  `\lezione` con le due letture.

### 3.5 Per data o per argomento
- **[S]** Un progetto è **per data** (le lezioni di un corso, in ordine di calendario) o **per argomento** (un libro, una
  dispensa, un corso senza calendario). Si sceglie alla creazione e si cambia quando si vuole dalle impostazioni: verso
  gli argomenti con Riorganizza (§3.11), verso le date tornando a un file per lezione. Un progetto vuoto cambia e basta.
- **[S]** In un progetto per argomento il materiale nuovo va nell'argomento con cui si sovrappone (con le stesse scelte:
  fondi, a parte, salta) o in un argomento nuovo, messo nel punto della scaletta dove sta meglio. Ogni pezzo ricorda la
  lezione da cui viene (`\lezione{N}{data}` prima del pezzo), così si torna sempre per data; ciò che non ha data torna
  come lezione senza data.

### 3.6 La conversione
La conversione non è più un tasto: è il modo in cui «Avvia» scrive una lezione nuova da una fonte sola.
- **[S]** **Va fino in fondo da sola.** Per ogni lezione: scaletta, trascrizione a blocchi, verifica, assemblaggio, figure, titolo, poi errori e righe, poi impaginazione delle box (se l'impostazione è «da sola»), poi compilazione del documento. Alla fine la lezione è *pronta* oppure *da rivedere*. Non si ferma ad aspettare la scelta delle figure: quelle da scegliere entrano nel registro.
- **[S]** Esito corto: una riga per lezione, l'impaginazione in una riga, il costo e i minuti, un collegamento al resoconto completo.
- **[S]** Un lavoro interrotto si riprende senza rifare ciò che è già fatto e pagato («Riprendi» in Attività).
- **[S]** Tipi di fonte:
  - scansioni e PDF (pagine come immagini);
  - documenti di testo;
  - `.tex` di altri autori: divisi per `\section{Lezione del …}` (due lezioni lo stesso giorno sono due lezioni), con le macro dell'autore in testa, figure annunciate, scaletta per blocchi di righe.
- **[S]** Figure dei `.tex`: «tieni» adatta il codice dell'autore alla colonna e lo asciuga solo se la resa resta uguale; «ridisegna» passa al disegnatore la resa del codice.
- **[S]** Stile «Da studiare» (prosa, correzioni dichiarate, aggiunte marcate, rimandi, didascalie) o «Fedele» (uno a uno, niente aggiunte; restano correzioni, passaggi troncati, box per esempi ed esercizi, didascalie, rimandi e titoli che la fonte ha).
- **[S]** I blocchi scritti hanno il marcatore di provenienza (§3.18).
- **[S]** L'agente legge testo, PDF e immagini PNG e JPEG; di `.docx`, `.odt`, `.doc`, `.rtf`, HEIC e TIFF riceve una copia leggibile.

### 3.7 Rivedi dubbi e Rivedi correzioni
Due pannelli con lo stesso codice, lezione per lezione. Il tasto nella barra c'è solo finché resta qualcosa da decidere (⇧⌘D, ⇧⌘E).
- **[S]** **Dubbi.** Mostra la domanda, la pagina della fonte (resa in immagine), il passo del `.tex` dietro «Sorgente» e il blocco che lo contiene.
  - Un clic su una lettura decide. Quella già scritta chiude e basta; le altre riscrivono il passo quando si trova una volta sola.
  - «Correggo io»: ricorda com'era il file e lo apre sul passo. Dopo il salvataggio mostra il prima e il dopo compilati (le parti cambiate in rosso e verde, la differenza del sorgente a richiesta), con «Modifica» e «Conferma».
  - «Decidi tu»: una chiamata per dubbio, con la pagina o il testo della fonte. Il modello scrive la sua lettura ma il dubbio aspetta la conferma. Se il modello lascia il passo com'è: «Nessuna modifica» e il perché.
  - «Fai decidere al modello tutti i dubbi», anche dal menu della lezione.
  - «Ricorda come regola del progetto» dopo una decisione diversa da ciò che era scritto.
- **[S]** **Correzioni.** Aggiunte e correzioni del modello: «Tieni», «Togli» (l'ambiente esce dal file e si ricorda per rimetterlo), «Tieni tutto». Ci sono anche errori e righe fuori misura, che portano a «Sistema».
- **[S]** **Riepilogo**: tutto, anche le lezioni fatte, in ordine di riga. Le voci da decidere e quelle decise con il loro come («✓ Scelta», «✓ Corretto da te», «✓ Deciso dal modello», «✓ Tenuta», «✓ Tolta»).
  - Un altro tasto cambia la scelta.
  - «Riapri» rimette la voce da decidere; «Annulla la modifica» rimette anche il passo.
  - «Riapri tutta» vale per una lezione. Riaprire qualcosa toglie «fatta» alla lezione.
- **[S]** **Tutto compilato**: domande, letture e aggiunte si vedono come nel PDF, con il preambolo del progetto, trasparenti e nel colore del tema. Intanto si vede il testo; un pezzo che non compila resta testo. La matematica fra `$…$` resta, «ω0» e «V_CB» diventano matematica.
- **[S]** Il pannello si rilegge quando torna in vista e a ogni ⌘S.
- **[C]** *(se i dubbi vengono da report in file)* I dubbi vecchi sono stringhe con una lettura sola; il passo si cerca nelle righe del blocco di quella pagina. *→ Vale solo se servono i progetti di prima (D1).*
- **[C]** *(prestazioni)* Una decisione deve costare decine di ms, non secondi. Serve una lista con le sole righe visibili, aggiornata riga per riga, e le immagini caricate in un thread. Ogni file dei dubbi si legge e si divide in blocchi una volta per ricarica, non una volta per dubbio.
- **[C]** *(se si fanno molte impronte)* `QCryptographicHash::hash` con OpenSSL ricarica il fornitore a ogni chiamata (millisecondi): il pannello si fermava per secondi. Si usa un oggetto per thread.

### 3.8 Figure
- **[S]** Un registro unico di progetto. Gli id restano per sempre (`<lezione>-NN`). La lezione di una figura è il `.tex` che ha il suo marcatore, non la fonte.
- **[S]** Figure delle scansioni: «Ridisegna» (TikZ, con revisione sulla resa) oppure «Ritaglia dalla fonte».
- **[S]** Rivedi figure (⇧⌘F, solo quando ce ne sono da scegliere o da inserire) si apre sulla prima da scegliere. Mostra tutte le figure del progetto, lezione per lezione, come «Figura N». Ogni scelta si salva subito. «Chiudi» inserisce quelle nuove; un esito si scrive solo se qualcosa non va, altrimenti basta una riga nella barra.
- **[S]** «Ricontrolla con le fonti»: l'agente rilegge tutte le fonti e propone un piano.
- **[S]** Riepilogo figure (menu Progetto): la panoramica.
- **[O]** Il motore delle figure è un servizio (`ReviewService`) chiamato con richieste in stile HTTP (`api/save`, `api/card`…) anche dentro l'app, attraverso `ApiClient`. Una porta locale serve per `texfix-cli imgcrop gui`. *→ Probabilmente sparisce: chiamate dirette al modello dei dati.*

### 3.9 Impaginazione

**[S]** **L'impaginazione è una filosofia che vale per tutto il documento, non solo per le box.** Il risultato deve
essere una pagina bella da studiare, con qualunque stile, anche senza nessuna box. Il programma trova ciò che si
impagina male e lo sistema con mosse misurate:
- una box spezzata male fra due pagine (o che si potrebbe spezzare in un punto migliore);
- un paragrafo che lascia una riga sola in fondo o in cima alla pagina, una frase troncata al cambio di pagina in un punto brutto;
- una formula separata dalla frase che la introduce o la commenta;
- un titolo in fondo alla pagina senza testo sotto;
- un elenco che lascia una voce sola dall'altra parte;
- una figura lontana da dove se ne parla, in un punto sbagliato, o che lascia un grande vuoto prima di sé; una didascalia separata dalla figura;
- grandi spazi vuoti in fondo alla pagina.

**[S]** Le regole sono quelle che oggi valgono per le box, generalizzate:
- si cercano i punti in cui il testo si può tagliare bene (le «cuciture»: fine di un paragrafo, di un riquadro, di un elenco…);
- per ogni problema si provano le mosse e si misura il risultato (vuoto lasciato nella colonna, distanza della figura dal suo richiamo…);
- si sceglie la mossa più pulita.

Le mosse possibili:
- a capo pagina in un punto scelto;
- una box o un paragrafo che si può spezzare lì;
- spazio verticale;
- misura esatta;
- posizione e misura di una figura;
- tenere insieme titolo e testo, formula e frase;
- «lascia com'era»;
- a mano.

Le mosse e le misure sono regole strutturali (§4.1); quanto essere severi è una regola editoriale.

**[S]** Si impagina da solo dopo Converti, se l'impostazione è «da sola». Si chiede all'utente solo ciò che resta incerto.
- **[S]** Un registro solo per progetto, sempre salvato, mai «chiuso». Si impagina solo ciò che è nuovo: le lezioni che il registro non ha o che sono cambiate dopo. Le decisioni prese restano.
- **[S]** Rivedi impaginazione (⇧⌘R, solo quando c'è qualcosa) e Riepilogo impaginazioni. Ogni mossa si accetta, si lascia com'era, si manda all'agente o si risolve a mano, e si cambia quando si vuole. Ogni scelta va subito nei file.
  - «Chiudi» chiude e basta, e si ricompila solo se un file è cambiato.
  - ⌘Z annulla l'ultima scelta del riepilogo quando il riepilogo è aperto.
  - Il tasto in alto torna indietro (panoramica ↔ scheda ↔ modifica a mano); Esc fa lo stesso.
- **[S]** Risolvere a mano:
  - le prove si compilano con SyncTeX; ⌘-clic su una pagina porta alla riga, ⌘J segna la riga del cursore;
  - la prima prova si compila da sola;
  - Progetto e chat si chiudono e tornano alla fine se c'erano;
  - con poco spazio le prove vanno sotto l'editor.
- **[S]** Il `\newpage` va prima del marcatore del blocco.
- **[C]** Si taglia anche dopo la fine di un riquadro o di un elenco, senza riga vuota. Una didascalia non si separa dalla sua figura.
- **[C]** Il controllo finale tocca e riferisce solo le lezioni del giro (prima toccava anche le altre).
- **[C]** Aprire la revisione non impagina da sola le lezioni ancora da fare.
- **[O]** Sessione a rami (`Branch`, `openBranch`, `rework`), comandi `redo/undo/repend/reset/ripristina`, `originali/` e `inizi/` per il reset, «Esci senza salvare» che aspetta il passo in corso. *→ Da ripensare: con la cronologia unica, i rami e i reset propri servono ancora?*

### 3.10 Errori e righe fuori misura
- **[S]** Righe troppo larghe o troppo vuote: prima le mosse deterministiche, poi il modello. «Sistema» funziona su una lezione o su tutto, e salta le lezioni fatte.
- **[S]** Errori di compilazione sistemati dal modello. Quando una lezione non fatta ha errori, nella barra in alto c'è «Risolvi errori» con il conto (il suggerimento dice quali lezioni): sistema le lezioni con errori, non quelle con sole righe fuori misura (decisione del 19/11/2026).
- **[O]** `Hbox::fixHbox` con schede, proposte, giri; test golden con compilazioni vere.

### 3.11 Riorganizza
- **[S]** **Per argomenti.** Il modello propone gli argomenti sulle sezioni del corso (fra un `\subsection` e il successivo; i `\paragraph` restano dentro). L'utente sistema la scaletta: rinomina, ordina, sposta, nuovo argomento. Il programma fa il piano, lo controlla e lo scrive. Lo stesso contenuto deve uscire riga per riga, e il documento deve compilare.
- **[S]** **Per data di lezione**, cioè il ritorno a un file per lezione. È attivo solo se i file sono per argomenti.
- **[S]** I marcatori dei blocchi seguono la loro unità.
- **[O]** Il ritorno a un file per lezione è una procedura dell'agente (`agente/reorganize.md`). *→ Probabilmente si può fare in modo deterministico, come l'andata.*

### 3.12 Lavori: piano, esito, archivio
- **[S]** Ogni lavoro con un punto di controllo ha un piano che si apre come documento fra le schede. Sono la fusione delle fonti, l'integrazione, Riorganizza, i dubbi all'agente, Ricontrolla le figure e lo smistamento. Il piano contiene:
  - riepilogo;
  - sezioni di voci con la spunta, modificabili, con istruzioni sotto ogni voce;
  - domande con opzioni;
  - commento.
  Ogni modifica si salva subito. C'è un tasto solo: «Avvia», oppure «Rivedi piano» se c'è un'istruzione o un commento. «Butta il piano» chiede conferma.
- **[S]** Esito leggibile alla fine. «Chiudi e archivia» archivia solo i piani eseguiti, in `salvataggi/esiti/`, con l'esito e il piano seguito. Il gruppo «Esiti» si apre impaginato.
- **[S]** I risultati che non dicono niente (impaginazione riuscita, figure inserite) sono una riga nella barra, non un esito.
- **[O]** L'agente scrive `piano.json` e stampa la riga `TEXFIX: piano pronto` / `TEXFIX: esito pronto`; l'app la legge dall'output. *→ Da ripensare: un'app nativa può dare all'agente uno strumento per consegnare il piano invece di un protocollo a righe.*

### 3.13 Agente e chat
- **[S]** Un pannello chat:
  - messaggi in streaming, con markdown e righe degli strumenti apribili;
  - modello per chat (Opus ↔ Sonnet) e permessi con nomi chiari;
  - allegati con miniature e screenshot di un pezzo della finestra;
  - ⌘↩ invia, ⌘L porta la selezione nel messaggio;
  - storico per progetto, con elimina a due clic;
  - riaprire una chat riprende la sua sessione, oppure riparte con la conversazione nel prompt.
- **[S]** **L'agente è isolato.** Vede solo il progetto e le regole e può usare gli strumenti del motore. Niente Python, `pdflatex`, git o `~/.claude`. Il prompt di sistema è `regole/agente/guida.md`.
- **[S]** Ogni procedura apre una sua chat con il suo nome.
- **[S]** Le lezioni fatte si toccano solo dopo una conferma.
- **[O]** È il CLI `claude` lanciato con `--setting-sources "" --disable-slash-commands --strict-mcp-config`. L'agente comunica con righe convenute (`TEXFIX: apri figure`, `TEXFIX: avanzamento N% — …`) e usa `texfix-cli` dalla shell. *→ Decisione D7: resta Claude Code locale (il CLI `claude`, con l'abbonamento dell'utente), per l'agente e per i worker. Decisione D2: `texfix-cli` serve solo all'agente, quindi non è più un programma per l'utente: diventa l'insieme di strumenti che l'app dà all'agente (la forma la sceglie l'architettura). Le righe convenute `TEXFIX:` sono da sostituire con strumenti.*

### 3.14 Cronologia (⌘Z, ⇧⌘Z)
- **[S]** Un solo modo di tornare indietro. Si annullano e si ripetono i lavori, le decisioni e le modifiche a mano: Modifica › Annulla «…», Ripeti «…», Cronologia del progetto (vai a un passo).
- **[S]** Un passo «Modifiche a mano» prima di ogni lavoro e uno con il nome del lavoro dopo. Converti con la sua impaginazione è un passo solo. All'apertura il primo passo è «Com'era all'apertura» oppure «Modifiche fuori da Texfix».
- **[S]** Si seguono il main, `generated.tex`, `.latexmkrc`, `src/`, `imgs/`, `fonti/` e le decisioni in `salvataggi/`. Non si seguono `docs/`, la chat, i lavori, gli esiti e `build/`.
- **[S]** Nell'editor ⌘Z resta del testo finché la sua pila ne ha, poi passa alla cronologia. Un ricaricamento da disco svuota la pila. Mentre un lavoro è in corso non si annulla niente. Una modifica dopo un annullamento toglie il «ripeti».
- **[S]** La cronologia vale per una sessione: si butta alla chiusura, e all'apertura si butta quella rimasta da una chiusura sbagliata. Al massimo 200 passi.
- **[C]** *(se il menu legge la cronologia da file)* Il menu deve fare una lettura sola, non quattro.

### 3.15 Impostazioni del progetto (⌘,)
- **[S]** Organizzazione: Per data · Per argomento (§3.5).
- **[S]** Stile: Da studiare · Fedele. Qualità: Veloce · Bilanciata · Massima (oggi i profili `sonnet-low`, `default`, `opus-medium`). Figure delle scansioni: Ridisegna · Ritaglia. Figure dei `.tex`: Tieni · Ridisegna. Impaginazione: da sola · quando lo chiedo.
- **[S]** Regole del progetto: entrano nei prompt di trascrizione e di decisione e l'agente le legge. Le segue la cronologia.
- **[S]** «Fatta» (menu della lezione): pallino verde con ✓, esce dai pannelli Rivedi; i lavori del modello chiedono conferma; le modifiche a mano restano libere; ⌘Z la annulla.

### 3.16 Version control
- **[S]** Una scheda fissa accanto a Progetto, con il numero delle modifiche nel titolo. Inizializza, commit (con conferma), cronologia, differenze affiancate. Nei progetti di appunti git lo usa solo l'utente.

### 3.17 Attività, barra di stato, Ferma, Riprendi
- **[S]** Una riga con la barra per ogni lavoro (Attività), la Console con il registro e gli Avvisi, nel pannello in basso.
- **[S]** Barra di stato:
  - a sinistra lo stato: spunta, cerchio che gira, × rossa con errori, triangolo con avvisi, pausa;
  - poi i messaggi brevi (la prima frase per 6 secondi, il resto in Console);
  - poi l'avanzamento del lavoro in corso con la percentuale;
  - «Ferma».
- **[S]** «Ferma» per ogni lavoro: subito, senza lasciare l'app «in corso».
- **[S]** «Riprendi» compare quando un lavoro lungo si è interrotto.
- **[O]** La pausa manda SIGSTOP ai processi e ai figli. *→ Decisione D6: la pausa sparisce; bastano «Ferma» e «Riprendi».*

### 3.18 Blocchi e tag di provenienza
- **[S]** Ogni unità di una lezione (box, figura, tratto di testo) ha prima un marcatore con id stabile, fonte e pagina (o righe di un `.tex`). L'id resta della lezione d'origine anche se il blocco cambia file. La pipeline lo scrive; quelli che mancano li aggiunge il programma dopo una fusione. Righe vuote, commenti e righe di struttura non sono blocchi.
- **[S]** Tag di provenienza nel PDF (sigla della fonte sulla cornice della box), da mostrare o nascondere, tutti o per sigla, con legenda. *Decisione D5: restano, ma ripensati in modo più sensato e integrato. Spunto: i marcatori dei blocchi sanno già fonte e pagina di ogni unità, quindi i tag potrebbero nascere da lì invece che da un meccanismo a parte, e collegarsi alla fonte (clic sul tag → la pagina della fonte).*

### 3.19 Interfaccia **[S]**
- **Un tema solo** (chiaro e scuro, segue il sistema) con colori e misure in un posto. *[O] Oggi prende i pastelli e gli angoli arrotondati di `styles`; con lo stile facoltativo il tema dell'app è suo.*
- **Tutto ci sta, su ogni schermo.** Le finestre non superano lo schermo e non scendono sotto 480×380; il contenuto scorre e i tasti in fondo restano visibili. Il testo non esce mai dal bordo: va a capo o si accorcia con «…», mai i tasti. Il pannello Progetto non scende sotto 200 px (all'avvio 240–300), la chat non sotto 280 px.
- **Scorrimento liscio**, fermo ai bordi, senza rimbalzi, senza campi che si prendono la rotella dentro una pagina che scorre.
- **La barra in alto**: fissi solo Compila e Converti. I quattro «Rivedi» compaiono solo quando c'è qualcosa da decidere.
- **I menu non ripetono i tasti.** Ciò che ha un tasto non sta nei menu ma tiene la sua scorciatoia.
- **Ciò che non si può fare è spento**, e la scorciatoia arriva comunque all'azione, che dice perché non può. Le condizioni leggono la memoria, non il disco.
- **Pannelli fissi**, niente staccare. A sinistra un pannello con due linguette (Progetto, Version control); chiuso diventa un'icona al bordo. La chat chiusa è un tasto in alto a destra. Il pannello in basso chiuso si riapre dalla barra di stato.
- **[C]** *(se si salva la disposizione delle finestre)* Uno stato salvato di una versione precedente si scarta. Prima faceva chiudere la finestra all'avvio.
- **[O]** Widgets per editor, PDF, albero e revisioni; QML per chat, Converti, benvenuto, attività, piano e Rivedi; ponte `Qml::widget/dialog`. *→ Decisione D4: QML dove possibile, Widgets solo dove servono davvero (editor, PDF); se l'architettura trova un approccio migliore e più sensato, si sceglie quello, motivandolo.*

---

## 4. Regole e stile

Anche le regole e il legame con `styles` vengono dalle skill: vanno riorganizzati, non riportati.

### 4.1 Le regole: strutturali nell'app, editoriali nelle preferenze

**[O]** Oggi `regole/` sono circa 1400 righe in markdown:
- 8 file di convenzioni;
- `distillazione.md` e `impaginazione.md`;
- le procedure dell'agente (`guida`, `lavori`, `distill`, `reorganize`, `revisione-box`, `review_all`, `ritaglio`);
- i prompt dei worker, con interruttori `{{#x}}` e segnaposto `@CONV:n@`;
- `srctag.tex`.

Tutto è mescolato: ciò che serve al programma per funzionare sta accanto a come l'utente vuole i suoi appunti, e
molte procedure spiegano all'agente comandi da terminale.

**[S]** Le regole si dividono in due:

| | **Strutturali** | **Editoriali** |
|---|---|---|
| Che cosa sono | Ciò senza cui il programma non funziona o si rompe | Come l'utente vuole i suoi appunti |
| Esempi | formato dei marcatori dei blocchi; forma dei dubbi, del piano e dell'esito che i worker e l'agente restituiscono; strumenti dell'agente e cosa non può fare; come si compila e quando un file è pulito; come si dichiarano le aggiunte e le correzioni; le mosse dell'impaginazione | prosa e tono; quanto aggiungere o correggere; notazione (es. grassetto per i vettori); quale box per che cosa; stile delle figure (colori, pgfplots, circuiti); larghezza delle righe; come si fondono le fonti |
| Dove stanno | **nel codice dell'app** (o in testi interni non modificabili), con i test | **nelle Preferenze**, leggibili e modificabili |
| Chi le cambia | chi sviluppa | l'utente |

- **[S]** Nelle Preferenze le regole editoriali si leggono come testo impaginato, si modificano e si riportano al predefinito, una sezione alla volta. Valgono per tutti i progetti.
- **[S]** Un progetto può aggiungere o cambiare regole sue: le «Regole del progetto» di §3.15. La gerarchia è una sola: predefinite dell'app → Preferenze → progetto. Un `CLAUDE.md` nella radice del progetto non serve più come meccanismo a parte.
- **[S]** Le regole editoriali che dipendono dallo stile (quale box per che cosa, i rimandi come tag, la notazione con le macro dello stile) vengono dallo stile del progetto (§4.2), non da un testo fisso.
- **[S]** I prompt dei worker e le istruzioni dell'agente si compongono da tre parti: la parte strutturale (dall'app), le regole editoriali (Preferenze e progetto) e le convenzioni dello stile. Nessun prompt ricopia una regola: la cita una volta.
- **[S]** Le procedure dell'agente che oggi spiegano comandi (`texfix-cli …`) diventano descrizioni di strumenti (D2) e si riducono a ciò che serve per decidere.
- **[O]** Varianti `v2`/`v3` dei prompt per versioni di `styles`. *→ Spariscono: lo stile del progetto dice ciò che serve.*

### 4.2 Lo stile del progetto: `styles` facoltativo e importabile

**[S]** **Un progetto ha uno stile alla volta, o nessuno.** Senza stile è un **classico documento LaTeX matematico**
(D10): `article`, teoremi, definizioni, dimostrazioni ed esempi con `amsthm`, nessun colore, nessuna dipendenza
esotica. È il punto di partenza e deve bastare per tutto ciò che Texfix produce.

**[S]** **Si può importare uno stile** (un `.sty`, come `styles`):
- si importa dalle Preferenze, in una libreria di stili, oppure direttamente in un progetto;
- il file va nella cartella del progetto, così il progetto compila anche senza Texfix;
- si può tornare a nessuno stile o passare a un altro.

**[S]** **All'importazione l'app fa domande all'utente** (D8). Legge il `.sty`, trova ambienti e macro, il modello
propone i ruoli e le convenzioni, e l'utente risponde. Il risultato è la descrizione dello stile, salvata accanto al
`.sty`; uno stile già descritto non chiede niente.

**[S]** **L'agente può lavorare sullo stile del progetto**: modificarlo o crearlo da zero mentre il progetto cresce
(un ambiente nuovo, un colore, una macro). Lo stile del progetto è un file del progetto come gli altri, seguito dalla
cronologia. Dopo una modifica la descrizione si aggiorna, e ciò che cambia ruolo diventa una domanda.

**[S]** **Profili di partenza.** Le scelte di un progetto (stile con la sua descrizione, regole editoriali,
impostazioni del progetto) si salvano come profilo e un progetto nuovo può partire da un profilo. Così una
configurazione provata si riusa.

**[S]** **Il progetto si adatta allo stile.** Texfix conosce di ogni stile:
- **il catalogo degli ambienti**, ciascuno con il suo ruolo (definizione, teorema, lemma, proposizione, dimostrazione, osservazione, esempio, esercizio, attenzione, riquadro generico, formula in evidenza…), gli argomenti e il colore;
- **le macro** che offre (insiemi numerici, norme, vettori…);
- **i comandi per figure e didascalie**, se ne ha;
- **come si comportano le box** fra una pagina e l'altra (si spezzano o no).

Da lì vengono le convenzioni dello stile che entrano nei prompt, i colori di begin ed end nell'editor e i nomi nei
pannelli. Lo stesso contenuto, con un altro stile, usa gli ambienti di quello stile.

**[S]** **Cambiare stile riconverte il progetto** (D9). Tutte le lezioni si riscrivono per il nuovo stile: gli ambienti
secondo i ruoli (definizione → l'ambiente di definizione del nuovo stile), le macro, i rimandi, poi si reimpagina. Ciò
che non ha un corrispondente diventa una domanda all'utente. È un lavoro con il suo piano e un passo solo della
cronologia.

**[S]** **Ciò che è di Texfix esce da `styles` ed entra in Texfix.** Funziona con qualunque stile, aggiunto dal
programma al preambolo (oggi `generated.tex`; la forma la sceglie l'architettura):
- i **tag di provenienza** (oggi `regole/srctag.tex` e il blocco `tag-provenienza`), ripensati come da D5;
- la **diagnostica dell'impaginazione** (oggi solo per le box: `\AllowBoxBreak`, `\BoxBreakWarnings…`, i segni di rottura): deve vedere i problemi del §3.9 con qualunque stile, anche senza box;
- le **aggiunte e correzioni** dichiarate (`addedtext`, `correction`): servono a «Rivedi correzioni»; il loro aspetto si adatta allo stile;
- i **segnaposto delle figure** (`\imgslot`) e `\lezione{N}{data}`;
- la **diagnostica delle righe** (`\MathWarningsOn/Off`, `\overfullrule`) usata per trovare le righe fuori misura.

**[S]** **`styles` si ripulisce** e diventa uno stile come gli altri, importabile. Il lavoro si fa nel suo repository
(`~/Table/styles`, su un branch suo: l'utente lo ha chiesto). La vecchia Texfix tiene la sua copia (`resources/styles.sty`) e i vecchi
progetti la loro, quindi non cambiano.

- **[C]** *(se il progetto non ha box)* L'impaginazione (§3.9) vale lo stesso per paragrafi, formule, titoli e figure; spariscono solo le mosse delle box.

**[S]** **Il progetto di prova** (quello su cui si prova la nuova app, mai un documento vero) usa il **nuovo `styles`
ripulito**, già descritto, e ha preferenze e regole editoriali che **riproducono il comportamento della vecchia
Texfix**: convenzioni di scrittura e notazione, box per ruolo, rimandi come tag, figure, larghezza delle righe,
impostazioni del progetto (Da studiare, Bilanciata, Ridisegna, Tieni, impaginazione da sola). Questa configurazione
si salva come primo **profilo di partenza**, pronto per i progetti nuovi dell'utente. È anche la prova che regole e
stile si separano bene: con questo profilo la nuova app deve scrivere appunti come la vecchia.
- **[C]** *(se lo stile non ha i rimandi come tag)* I rimandi usano `\cref`.
- **[C]** *(se lo stile cambia i colori)* I colori di begin ed end nell'editor e nei pannelli seguono lo stile; il tema dell'app resta il suo (i pastelli di `styles` non sono più il tema dell'app).

---

## 5. Prove e accettazione

- **[O]** `tests/golden/` della vecchia app: uscite di riferimento del motore (assemblaggio, figure, oltre 8000 casi delle funzioni pure, hbox con compilazioni vere, prompt resi). *→ Con regole e stile riorganizzati (§4) molte uscite cambiano per scelta: valgono come riferimento per le funzioni pure e per i comportamenti che non dipendono dallo stile e dai prompt; per il resto la nuova app scrive i suoi test.*
- **[S]** Prove senza mani dell'interfaccia: foto della finestra dopo una sequenza di gesti, anteprime dei singoli pannelli, preferenze di prova separate da quelle vere. La forma la decide l'architettura.
- **[S]** Misure (`texfix_check bench`): i tempi di ciò che l'interfaccia rifà spesso non devono peggiorare.
- **[S]** Mai provare su un documento vero: il progetto di prova o una copia.
- **[V]** Niente Python nel programma, nei test e negli strumenti.
- **[C]** La build da zero con troppi processi blocca un Mac da 8 GB: al massimo 4 in parallelo.

---

## 6. Eredità delle skill da non riportare

Ciò che la vecchia app fa per com'è nata, non per ciò che serve all'utente. Nella nuova app non deve esserci, salvo
motivo nuovo.

1. **L'app che parla al motore lanciando `texfix-cli`** e leggendo il suo output: righe `@@avanzamento`, JSON in coda all'uscita (`jsonTail`), `status.json` scritto da un processo e letto da un altro, codici d'uscita (0/5…). Il motore diventa una libreria chiamata direttamente; all'agente si danno strumenti (D2).
2. **Servizi in stile HTTP dentro il processo** (`ReviewService`, `ApiClient`, `api/save`, `api/card`) e la porta locale (`HttpServer`).
3. **Sessioni, rami, reset e backup propri** di ogni motore, accanto alla cronologia unica.
4. **Protocollo a righe con l'agente** (`TEXFIX: …`) invece di strumenti.
5. **Migrazioni sparse** (`unifyQueues` e `moveSaves` a ogni apertura e a ogni comando del motore, `renameBlocks`, `ImgCrop::migrate`, `adoptOldMain`…).
6. **Nomi e strutture del motore modellati sugli script** (stadi numerati, «pipe», «seed», varianti `v2`/`v3` dei prompt per versioni vecchie di `styles`).
7. **Lettura guidata, ritmo MANUALE/AUTO/STOPPED, `pipe wait`**: già quasi tolti, non vanno riportati.
8. **Stato duplicato**: le stesse informazioni ricostruite da più fonti (manifest, report, nomi dei file, STATO.md, `salvataggi/lezioni/*.json`). Nella nuova app un modello del progetto in memoria, una fonte per ogni dato.

---

## 7. Decisioni dell'utente (14/11/2026)

| | Domanda | Decisione |
|---|---|---|
| D1 | Progetti di prima | Niente da migrare: i progetti esistenti restano alla vecchia app. La nuova sceglie il suo formato. |
| D2 | `texfix-cli` dal terminale | Serve solo all'agente: diventa strumenti per l'agente, non un programma per l'utente. |
| D3 | Lavori dopo la chiusura | Chiusa l'app, tutto si ferma; alla riapertura il lavoro riprende senza ripagare. |
| D4 | Interfaccia | QML, Widgets solo dove servono; un approccio migliore e motivato è benvenuto. |
| D5 | Tag di provenienza | Restano, ripensati in modo più sensato e integrato. |
| D6 | Pausa | Tolta: bastano Ferma e Riprendi. |
| D7 | Agente | Claude Code locale (il CLI `claude`). |

Decisioni aggiunte lo stesso giorno: le regole si dividono fra strutturali (nell'app) ed editoriali (nelle
Preferenze); `styles` diventa facoltativo e importabile, con uno stile predefinito semplice e classico, e ciò che è di
Texfix esce da `styles` (§4).

Decisioni successive (14/11/2026):
- uno stile alla volta o nessuno; cambiare stile riconverte il progetto;
- l'agente può modificare o creare lo stile del progetto;
- **D8, D9**: all'importazione e alla riconversione l'app fa domande all'utente, che risponde;
- **D10**: senza stile il progetto è un classico documento LaTeX matematico;
- il progetto di prova usa il nuovo `styles` con preferenze che riproducono la vecchia Texfix, e diventa il primo profilo di partenza;
- l'impaginazione vale per tutto il documento, non solo per le box (§3.9).

Decisioni del 18/11/2026 (§3.4, §3.5): una porta sola per il materiale, Aggiungi materiale, con il piano sempre mostrato;
il progetto è per data o per argomento, scelto alla creazione e cambiabile quando si vuole; il riconoscimento usa il
modello a ogni aggiunta, ma niente fallisce in silenzio; si smista per concetti, con il grafo delle intersezioni in cima
al piano, e la qualità viene prima del costo.

Decisioni del 23/11/2026 (§3.4): Aggiungi materiale mette solo in arrivo, senza far partire niente; il riconoscimento e
il piano partono con «Smista».

Le domande D8–D10 come erano poste:
- **D8. Come Texfix conosce uno stile importato.** (a) Lo stile porta un piccolo file di descrizione (ambienti e ruoli, macro, colori) accanto al `.sty`; `styles` lo avrà, per gli altri lo si scrive all'importazione. (b) All'importazione Texfix legge il `.sty`, trova ambienti e macro, il modello propone i ruoli e l'utente li conferma; il risultato si salva come in (a). Proposta: (b), che produce (a), così ogni stile funziona e quelli già descritti non costano niente.
- **D9. Cambiare stile a lezioni già scritte.** Riscrivere gli ambienti secondo i ruoli, con un passo di cronologia, va bene? Che cosa si fa di un ambiente senza corrispondente: lo sceglie l'utente caso per caso, o si usa il riquadro generico?
- **D10. Lo stile predefinito.** «Semplice e classico» come `amsthm` su `article` (teoremi in corsivo, dimostrazioni con ∎, nessun colore), o con riquadri leggeri? Un esempio di documento che ti piace aiuterebbe.

Le domande come erano poste:

- **D1. Progetti di prima.** Hai ancora progetti nel formato precedente al 10/11/2026 (`docs/to_be_converted`, seed delle figure, salvataggi in `docs/` o `build/`, dubbi come stringhe)? Se no, la nuova app legge solo il formato del §2 e tutte le migrazioni spariscono. Se sì, quali progetti?
- **D2. `texfix-cli` dal terminale.** Lo usi tu, o serve solo all'agente? Se serve solo all'agente, può diventare un insieme di strumenti dati all'agente direttamente.
- **D3. Lavori che sopravvivono alla chiusura.** Oggi una conversione continua anche se chiudi Texfix. Ti serve, o basta che riprenda da dove era (senza ripagare) alla riapertura?
- **D4. Interfaccia.** Va bene tutta in QML (più semplice da tenere coerente), tenendo i Widgets solo dove servono davvero (editor, PDF)?
- **D5. Tag di provenienza nel PDF.** Li usi ancora?
- **D6. Pausa** (sospendere un lavoro e riprenderlo). La usi, o basta «Ferma» e «Riprendi»?
- **D7. Agente.** Resta il CLI `claude` (usa il tuo abbonamento) o si passa all'Agent SDK? Il CLI con l'abbonamento è probabilmente la scelta giusta per i costi; chiedo per conferma.

---

## 8. Cosa non è ancora verificato

Questa specifica viene dalla documentazione, dalla cronologia e dalle intestazioni, non dalla lettura riga per riga di
tutti i 36.000 righe. Prima di scrivere ciascun modulo nuovo si legge il suo corrispondente vecchio per:
- lo schema esatto dei JSON del §2;
- gli stadi di Converti e cosa passa fra loro (`Pipe.cpp`, `PipeCtx`, `Worker`);
- le mosse dell'impaginazione e le cuciture (`BoxFixCore`, `BoxFixLesson`, `regole/impaginazione.md`);
- le mosse deterministiche delle righe (`Hbox`);
- i comandi di `texfix-cli` usati dall'agente (`regole/agente/*.md`).

Un caso limite trovato lì si aggiunge al §3 con il suo segno **[C]** e la sua condizione.
