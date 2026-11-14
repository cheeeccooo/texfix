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

**[S]** Un programma per macOS che trasforma appunti di ogni tipo in appunti LaTeX da studiare, scritti con il
pacchetto `styles`. Le fonti possono essere scansioni a mano, PDF di slide, documenti o `.tex` di altri autori. Un modello
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
- **[S]** Nuovo progetto: nasce intero, con `styles.sty`, `main.tex`, le cartelle, `generated.tex`, `.latexmkrc` e il repository git.
- **[S]** Apri cartella. Una cartella che non è un progetto si prepara con il sì dell'utente: ciò che c'era va in `docs/`, un nome già preso diventa «nome (2)» e i file nascosti restano dove sono. Un progetto senza `main.tex` si completa da sé.
- **[S]** Pagina di benvenuto con i progetti recenti.
- **[S]** Un'istanza sola. Un secondo avvio passa la cartella a quella già aperta.
- **[S]** Le lezioni sono una regola sola: gli `\input{src/…}` non commentati dopo `\begin{document}` di `main.tex`, in ordine. Quelle che mancano su disco si saltano.
- **[C]** *(se si leggono gli input dal main)* Vanno gestiti: un secondo documento nella cartella, un `\input` nel preambolo, uno commentato, uno che manca, l'ultima riga senza a capo. Prima c'erano quattro ricerche del main diverse e in disaccordo. Test: caso `main`.

### 3.2 Editor, PDF, compilazione
- **[S]** Editor LaTeX con evidenziazione. Begin ed end delle box sono scritti nel colore della box nel PDF, letto da `styles`; eqbox e genericbox in grigio chiaro.
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
- **[S]** Gruppi dell'albero: Lavori; Lezioni (o Argomenti); Da convertire; Documento (main, `generated.tex`, file fuori dal main); Fonti senza lezione; Da consultare (`docs/`, ci si trascinano file); Esiti; Immagini (chiuso, divise per il file che le usa).
- **[S]** Una lezione è una riga: pallino dello stato, numero, titolo, data. Il pallino è verde (pronta, con ✓ se fatta), giallo (da rivedere), rosso (errori) o grigio (mai compilata). Dentro ci sono le righe Dubbi, Correzioni, Impaginazione, Figure con il loro conto (un clic apre il pannello su quella lezione) e il ramo Fonti.
- **[S]** Il nome sta a sinistra; a destra, piccolo, il dettaglio (data, conto, stato). Ogni voce ha il nome intero nel suggerimento.
- **[S]** Un clic apre ciò che va in una scheda; il doppio clic apre anche ciò che prende la finestra o un'altra app; la freccia apre solo il ramo. Ciò che l'utente apre o chiude resta così fra un aggiornamento e l'altro.
- **[S]** Lo stato di una lezione mette insieme `\lezione`, le fonti, i dubbi aperti, le aggiunte da tenere, le figure da scegliere o inserire e gli avvisi dell'ultima compilazione. Il documento intero aggiorna tutte le lezioni, un file solo aggiorna la sua.
- **[C]** *(se le fonti si ritrovano per nome)* «notes_LabSS (1).pdf» deve ritrovare notes_LabSS_1.pdf: si confronta il nome senza maiuscole, punteggiatura e « (2)». Questa regola sta in un posto solo (`Sources::bare`).
- **[C]** Le lezioni di prima senza manifest si riconoscono dal nome `srcN.*` o dalla data nel nome dei file in `fonti/usate`.

### 3.4 Nuova lezione
- **[S]** Numero e data vengono proposti. Le fonti si mostrano come miniature (trascinate, dal disco o dalla coda); tasto destro: apri, dai importanza, togli. La lezione compare subito nell'albero.
- **[S]** «Prendi dal file»: numero e data cercati senza modello nei nomi e nel testo delle fonti; ciò che non trova lo chiede. Se le fonti non concordano o il numero c'è già, lo dice. «Crea» avvisa se le fonti dicono altro; un secondo clic tiene i valori scritti.
- **[S]** Una fonte: si converte. Più fonti senza intersezioni (deciso da una chiamata leggera al modello: cosa hanno in comune, l'ordine, la più ricca): si uniscono e si convertono, con una riga in testa all'esito. Fonti che si sovrappongono o di tipo diverso: le fonde l'agente, **sempre con un piano da approvare**.
- **[S]** Non c'è gerarchia fra le fonti: la struttura la dà la fonte più ricca, le fonti «importanti» decidono i dubbi.
- **[S]** «Aggiungi fonti a questa lezione»: l'agente integra solo ciò che manca, con un piano.
- **[S]** Se il numero e la data letti dalla scansione non tornano con quelli scelti, si crea un dubbio sulla riga `\lezione` con le due letture. Nella fusione diventa una domanda del piano.
- **[S]** A lavoro finito le fonti vanno in `fonti/usate/lezione N/` e restano sotto la lezione. Tutto il lavoro è un passo solo della cronologia.
- **[S]** Versioni delle fonti. Un file con il nome di una fonte già usata: se è identico non si aggiunge; se è diverso si chiede se è la versione nuova. Se sì, la vecchia va in `precedenti/` con la data, la nuova prende il suo posto e una copia va in coda per lo smistamento. L'agente sa che è un aggiornamento e porta le parti nuove e le correzioni. A mano: «Carica una versione nuova…». Un file identico non si archivia due volte.

### 3.5 Aggiungi materiale (smistamento)
- **[S]** File aggiunti o trascinati sull'albero vengono smistati fra lezioni nuove e lezioni da completare. Ogni pagina finisce in un posto solo, oppure in un file per lezione.
- **[O]** Due chiamate: per ogni file di scansioni una con le miniature (data e argomento di ogni pagina, `prompt/smistamento-pagine.md`), poi una leggera con le lezioni del corso (`prompt/smistamento.md`).
- **[S]** Il piano: una sezione per lezione, una voce per pezzo, scritto a parole («p1.tex · lezione del 29 settembre 2026»). Sotto ogni voce un campo di istruzioni. Con almeno un'istruzione il tasto diventa «Rivedi piano»: si rifà solo l'ultima chiamata e le istruzioni restano sul loro pezzo. «Avvia» divide i PDF in pezzi e crea le lezioni, che partono una dopo l'altra. Una lezione nuova con istruzioni passa dall'agente. Le lezioni da completare passano dall'agente con il piano. «Butta il piano» chiede conferma e il materiale resta in coda. Chiudere la scheda lascia il piano sotto Lavori.
- **[C]** *(se le lezioni partono in coda)* Una fusione aspetta che il piano aperto dell'agente sia deciso.

### 3.6 Converti
- **[S]** Le fonti in «Da convertire» diventano lezioni nuove. Un `.tex` con più lezioni dà una riga per lezione, in ordine di calendario. Le lezioni di un `.tex` che il corso ha già non si elencano: una riga le conta e «Fai il piano» le manda ad Aggiungi materiale. Se c'è un piano di materiale da approvare, Converti apre quello.
- **[S]** In basso: riepilogo delle impostazioni del progetto e costo stimato (da pagine e righe). «Cambia» apre le impostazioni e ciò che si cambia lì vale per il progetto.
- **[S]** **Converti va fino in fondo da solo.** Per ogni lezione: scaletta, trascrizione a blocchi, verifica, assemblaggio, figure, titolo, poi errori e righe, poi impaginazione delle box (se l'impostazione è «da sola»), poi compilazione del documento. Alla fine la lezione è *pronta* oppure *da rivedere*. Non si ferma ad aspettare la scelta delle figure: quelle da scegliere entrano nel registro.
- **[S]** Esito corto: una riga per lezione, l'impaginazione in una riga, il costo e i minuti, un collegamento al resoconto completo.
- **[S]** Un lavoro interrotto si riprende senza rifare ciò che è già fatto e pagato.
- **[S]** Tipi di fonte:
  - scansioni e PDF (pagine come immagini);
  - documenti di testo;
  - `.tex` di altri autori: divisi per `\section{Lezione del …}`, con `--only file.tex@AAAA-MM-GG`, macro dell'autore in testa, figure annunciate, scaletta per blocchi di righe.
- **[S]** Figure dei `.tex`: «tieni» adatta il codice dell'autore alla colonna e lo asciuga solo se la resa resta uguale; «ridisegna» passa al disegnatore la resa del codice.
- **[S]** Stile «Da studiare» (prosa, correzioni dichiarate, aggiunte marcate, rimandi, didascalie) o «Fedele» (uno a uno, niente aggiunte; restano correzioni, passaggi troncati, box per esempi ed esercizi, didascalie, rimandi e titoli che la fonte ha).
- **[S]** I blocchi scritti hanno il marcatore di provenienza (§3.18).
- **[O]** `pipe run` gira staccato (`--detach`) con cache su disco; l'app segue `build/artifacts/pipe/status.json`. Ci sono stadi numerati 1–9 con worker `claude -p` in parallelo e uno stato condiviso (`RunState`). *→ Decisione D3: il motore gira nell'app; chiusa l'app, tutto si ferma. Alla riapertura il lavoro riprende da dove era, senza rifare né ripagare ciò che è già fatto (cache su disco del lavoro).*
- **[O]** La «lettura guidata» (un file alla volta in attesa della revisione dei ritagli) resta solo per riprendere run vecchi. *→ Sparisce.*

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

### 3.9 Impaginazione delle box
- **[S]** Una box spezzata fra due pagine si sistema con delle mosse: a capo pagina prima della box, box che si può spezzare, spazio verticale, misura esatta, «lascia com'era», immagini, titolo, a mano. Le regole stanno in `regole/impaginazione.md`.
- **[S]** Un registro solo per progetto, sempre salvato, mai «chiuso». Si impagina solo ciò che è nuovo: le lezioni con box spezzate che il registro non ha, o che sono cambiate dopo. Le decisioni prese restano.
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
- **[S]** Errori di compilazione sistemati dal modello.
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
- **Un tema solo** (chiaro e scuro, segue il sistema) con colori e misure in un posto; i pastelli e gli angoli arrotondati di `styles`.
- **Tutto ci sta, su ogni schermo.** Le finestre non superano lo schermo e non scendono sotto 480×380; il contenuto scorre e i tasti in fondo restano visibili. Il testo non esce mai dal bordo: va a capo o si accorcia con «…», mai i tasti. Il pannello Progetto non scende sotto 200 px (all'avvio 240–300), la chat non sotto 280 px.
- **Scorrimento liscio**, fermo ai bordi, senza rimbalzi, senza campi che si prendono la rotella dentro una pagina che scorre.
- **La barra in alto**: fissi solo Compila e Converti. I quattro «Rivedi» compaiono solo quando c'è qualcosa da decidere.
- **I menu non ripetono i tasti.** Ciò che ha un tasto non sta nei menu ma tiene la sua scorciatoia.
- **Ciò che non si può fare è spento**, e la scorciatoia arriva comunque all'azione, che dice perché non può. Le condizioni leggono la memoria, non il disco.
- **Pannelli fissi**, niente staccare. A sinistra un pannello con due linguette (Progetto, Version control); chiuso diventa un'icona al bordo. La chat chiusa è un tasto in alto a destra. Il pannello in basso chiuso si riapre dalla barra di stato.
- **[C]** *(se si salva la disposizione delle finestre)* Uno stato salvato di una versione precedente si scarta. Prima faceva chiudere la finestra all'avvio.
- **[O]** Widgets per editor, PDF, albero e revisioni; QML per chat, Converti, benvenuto, attività, piano e Rivedi; ponte `Qml::widget/dialog`. *→ Decisione D4: QML dove possibile, Widgets solo dove servono davvero (editor, PDF); se l'architettura trova un approccio migliore e più sensato, si sceglie quello, motivandolo.*

---

## 4. Ciò che non è codice **[V]**

- **`regole/`**: convenzioni (8 file), prompt dei worker in inglese con interruttori `{{#x}}…{{/x}}` e segnaposto `@CONV:n@`, `@STEM@`…, procedure dell'agente. Sono il prodotto: si possono spostare o accorpare, non cambiarne il senso. I prompt resi sono controllati byte per byte dai test golden.
- **`styles`** si sviluppa in `~/Table/styles`; `resources/styles.sty` è una copia rilasciata da lì.
- **Un `CLAUDE.md` nella radice di un progetto di appunti** prevale sulle regole dove dice altro.

---

## 5. Prove e accettazione

- **[V]** `tests/golden/` della vecchia app: uscite di riferimento del motore (assemblaggio, figure, oltre 8000 casi delle funzioni pure, hbox con compilazioni vere, prompt resi). Sono la **prova di accettazione** del motore nuovo: stesse uscite, salvo cambiamenti voluti e registrati.
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
