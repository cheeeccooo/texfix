# Texfix (nuovo)

La riscrittura di Texfix come app nata app. Che cosa deve fare: `SPEC.md`. La vecchia app (`../texfix`) è il riferimento
quando un dettaglio manca: si legge il suo codice prima di scrivere il modulo corrispondente (SPEC §8), non lo si copia.
Questo file descrive il codice com'è: si aggiorna insieme al codice.

## Comandi

```sh
tools/nuova-prova.sh [cartella] [profilo]   # ricrea il progetto di prova (../prova-texfix-nuovo) con lo stile styles;
                                           # con «profilo» salva il primo profilo di partenza fra i dati veri
./build.sh                    # configura (la prima volta) e compila in build/, al massimo 4 processi
./build.sh prove              # compila e lancia le prove del motore (build/texfix_tests)
./build.sh misure [--scrivi]  # misura i tempi di ciò che l'interfaccia rifà spesso (build/texfix_bench)
./build.sh avvia [cartella]   # compila e apre il programma
build/Texfix.app/Contents/MacOS/Texfix <cartella> --foto f.png [--gesti "apri src/src1.tex 12; compila"] [--dimensione 1000x620]
```

Dipendenze: `brew install cmake qtbase qtdeclarative qtwebengine` (QtPdf), MacTeX. Niente Python, da nessuna parte.

## Architettura

Due parti, un processo solo.

- **`src/core/` — il motore** (`texfix_core`, namespace `tx`): una libreria senza finestre, chiamata direttamente dal
  programma e dalle prove. Niente eseguibile a riga di comando, niente protocolli fra processi. I soli processi figli
  sono i programmi esterni (latexmk, git; poi il CLI `claude`).
- **`src/app/` + `qml/` — il programma**: una finestra sola tutta in Qt Quick (D4). I tipi C++ di `src/app` sono il
  modello che QML legge (`Workspace`, `Theme`) e ciò che QML non fa da sé (`EditorBackend`, `LineGutter`, `PdfBackend`).
  Niente Widgets: l'editor è un `TextEdit` con un `QSyntaxHighlighter` sul suo documento, il PDF una lista di pagine
  disegnate da `QPdfDocument` come immagini. Così c'è un tema solo, una scena sola e un solo modo di fotografarla.

| Modulo | Che cosa fa |
|---|---|
| `core/Files` | letture; scritture sicure (`QSaveFile`); nomi liberi «nome (2)» |
| `core/Tools` | il PATH dei programmi esterni; processi a capo di un gruppo (`ownGroup`, `stopGroup`: lui e i figli) |
| `core/Project` | il progetto su disco: crea, adotta una cartella, completa ciò che manca; la regola delle lezioni (`inputs`) |
| `core/TexLog` | gli avvisi di un `.log`: errori con file e riga, righe troppo larghe o vuote |
| `core/Compiler` | latexmk: il documento, o una lezione da sola con il main ridotto; avanzamento, «Ferma», sorveglianza |
| `core/SyncTex` | il salto sorgente ↔ PDF, con il programma `synctex` e il `.synctex.gz` accanto al PDF |
| `core/History` | la cronologia del progetto: passi in memoria, contenuti in `build/cronologia/`, buttati alla chiusura |
| `core/Settings` | `salvataggi/progetto.json`: le cinque scelte del progetto, le regole del progetto, lo stile, le lezioni fatte |
| `core/Rules` | le regole editoriali: predefinite (`regole/editoriali/*.md`, dentro il programma) → Preferenze → progetto; `forPrompt` |
| `core/Style` | la descrizione di uno stile (ambienti con ruolo e colore, macro, rimandi, figure, convenzioni); `classic()`; `scan` di un `.sty` |
| `core/StyleSwitch` | importare uno stile nel progetto; cambiare stile riscrivendo le lezioni secondo i ruoli |
| `core/Job` | `JobControl`: «Ferma», avanzamento e registro di un lavoro che gira in un thread suo; `Stopped`, `LimitReached` |
| `core/Model` | le chiamate al modello: `claude -p` senza strumenti, al più 6 insieme; modello e impegno per qualità e fase; costi. `TEXFIX_CLAUDE=<programma>` mette un finto modello (prove) |
| `core/Pages` | una fonte come la vede il modello: pagine in immagini (pdftoppm, sips) o testo |
| `core/Blocks` | i marcatori dei blocchi `% @L6.3 · fonte p.2`: leggere, aggiungere quelli che mancano |
| `core/Prompts` | i prompt, in tre parti: strutturale (qui), `rules::forPrompt`, `Style::forPrompt` |
| `core/Convert` | Converti: scaletta, blocchi trascritti e verificati insieme, assemblaggio, lezione nel main, fonte in `fonti/usate/lezione N/`, dubbi in `salvataggi/dubbi/<nome>.json`; cache in `build/lavori/converti/` |
| `core/Snippets` | la resa compilata di piccoli pezzi (domande, letture, aggiunte) per i pannelli Rivedi: immagini trasparenti in `build/resa/` |
| `core/Review` | ciò che resta da decidere: i dubbi (`salvataggi/dubbi/`: scegli una lettura, riapri, annulla la modifica) e le aggiunte e correzioni dichiarate (`salvataggi/correzioni.json`: tieni, togli, rimetti); i conti per lezione |
| `core/Figures` | il registro delle figure (`salvataggi/figure.json`); il segno `% img <lezione>-NN` nella lezione; «Ridisegna» (TikZ: disegno, prova con `pdflatex`, revisione sulla resa) e «Ritaglia dalla fonte» (`imgs/<nome>.png`); i pacchetti chiesti dai disegni vanno in `progetto.json` e da lì in `texfix.sty` |
| `core/Fix` | errori e righe fuori misura di una lezione: gli errori al modello (sostituzioni esatte), le righe prima spezzando il paragrafo a una fine di frase poi con varianti del modello; ogni mossa resta solo se, compilata (`Compiler::compileSync`), migliora |
| `core/Layout` | l'impaginazione, con qualunque stile: dove cade ogni blocco nel PDF (SyncTeX, e l'area del testo scritta nel log da `texfix.sty`); un'unità a cavallo di due pagine (o colonne) va a pagina nuova (`\newpage % impaginazione` prima del marcatore) se il vuoto è al più il 25%; altrimenti, o se è più lunga di una colonna, si spezza in un punto scelto: il comando dello stile (`comando_spezza`: `\tcbbreak` per `styles`, `\pagebreak` senza stile) sull'ultima cucitura fra due paragrafi prima del punto in cui passerebbe alla colonna dopo (se le pagine in piccolo del prima e del dopo sono uguali la mossa ha il segno «invariata»: `same`, `invariata` nel registro); se nessuna mossa regge resta proposta; registro `salvataggi/impaginazione.json` |
| `core/AgentTools` | gli strumenti che l'app dà all'agente (`stato_progetto`, `compila_lezione`, `compila_documento`, `dubbi`, `impagina`), il protocollo a righe (MCP) e il prompt di sistema dell'agente |
| `core/Plans` | i lavori con un piano: `salvataggi/lavori/<nome>/piano.json` (voci con spunta e istruzioni, domande, commento, esito); l'agente consegna piano ed esito con gli strumenti `consegna_piano` e `consegna_esito`; l'archivio in `salvataggi/esiti/` |
| `core/NewLesson` | numero e data proposti o letti dai nomi e dal testo delle fonti (senza modello); i file portati in `fonti/coda`; `settle` chiude una lezione scritta dall'agente (documento, marcatori, fonti usate: lo strumento `chiudi_lezione`); i messaggi delle procedure di fusione e integrazione |
| `core/Sort` | Aggiungi materiale: data e argomento di ogni pagina (una chiamata per file, tenuta in `build/lavori/smistamento/`), poi una chiamata con le lezioni del corso; il piano (tipo «materiale») e `prepare`, che divide i PDF in pezzi e dice che cosa si converte e che cosa passa dall'agente |
| `core/Git` | il version control dell'utente: modifiche, differenze, commit di tutto, cronologia (chiama `git`) |
| `core/Reorganize` | Riorganizza: le sezioni del corso (fra un `\subsection` e il successivo), gli argomenti proposti dal modello, la scrittura di un file per argomento e il ritorno a un file per lezione, tutti e due del programma; resta uguale il contenuto riga per riga, o non si tocca niente |
| `core/TexSource` | le fonti `.tex` di altri autori: le lezioni per data (`\section{Lezione del …}`), le macro dell'autore, il testo per il modello; una lezione si indica con `file.tex@AAAA-MM-GG` |
| `core/Profiles` | i profili di partenza: stile (il `.sty` e la descrizione), impostazioni e regole del progetto salvati con un nome fra i dati del programma (`profili/<nome>/`); un progetto nuovo può partire da lì |
| `core/Provenance` | i tag di provenienza (D5): le fonti con la loro sigla e, per ogni blocco con una fonte nel marcatore, dove cade nel PDF (SyncTeX). Niente nel documento: li disegna la vista del PDF |
| `app/Chat` | la chat: il CLI `claude` chiuso nel progetto (file sì, shell no, nessuna impostazione dell'utente), messaggi in streaming, strumenti eseguiti dal motore in un thread; `Texfix --strumenti <nome>` è il passacarte fra il CLI e il programma, su un canale locale; storico in `salvataggi/chat/` |
| `app/Workspace` | il progetto aperto per la finestra (singleton QML): lezioni con lo stato, avvisi, PDF, compilazione |
| `app/Theme` | l'unico posto con colori e misure (singleton QML); chiaro e scuro col sistema, `TEXFIX_TEMA=chiaro\|scuro` |
| `app/EditorBackend`, `LatexHighlighter`, `LineGutter` | evidenziazione, righe ↔ posizioni, numeri di riga |
| `app/PdfBackend` (+ `PdfPages`) | pagine e misure di un PDF; le pagine come `image://pdf/<revisione>/<pagina>/<pixel>/<percorso>` |
| `qml/` | `Main` (finestra, menu, scorciatoie), `Welcome`, `ProjectView` (barra, pannelli), `ProjectPanel`, `EditorPane` (schede: `TexEditor`, `PdfPane`, `ImageView`), `BottomPanel`, `StatusBar`; componenti `Tx…` |

## Il progetto su disco (formato nuovo, D1)

```
main.tex            il documento; le lezioni sono i suoi \input{src/…} dopo \begin{document}
texfix.sty          lo stile del progetto (\RequirePackage{<stile>}, o senza stile il classico amsthm) e ciò che è di
                    Texfix (oggi \lezione); lo scrive il programma per intero. main.tex carica solo questo
<stile>.sty         lo stile importato, con accanto <stile>.stile.json, la sua descrizione
.latexmkrc          creato se manca; quello dell'utente non si tocca
src/ imgs/          lezioni e immagini
fonti/coda|usate/   fonti da convertire e già usate
docs/               documenti di consultazione
salvataggi/         ciò che Texfix ricorda; progetto.json (formato, impostazioni, regole, stile, fatte) dice che la
                    cartella è un progetto
build/              ciò che si rifà: main.pdf, lezioni/<nome>.pdf, stato-lezioni.json, cronologia/
```

Senza stile il documento è un classico `article` con `amsthm` (D10), definito in `texfix.sty`: ambienti `theorem`, `proposition`, `lemma`,
`corollary`, `definition`, `example`, `exercise`, `remark`, `proof`.

Un dato, una fonte: numero, data e titolo di una lezione si leggono dal suo `.tex` (`\lezione{N}{data}`, il primo
`\section`); l'elenco delle lezioni da `main.tex`. Nessun file li ripete.

## Regole per chi sviluppa

- `build/texfix_tests` deve passare per intero. Le prove lavorano in cartelle temporanee, senza modello.
- Mai provare su un documento vero. Le prove dell'interfaccia usano `TEXFIX_PREFERENZE=<file>` (preferenze separate) e
  `TEXFIX_DATI=<cartella>` (profili e regole delle Preferenze separati da quelli veri).
- Interfaccia, commenti e documenti in italiano, senza gergo (SPEC §1: mai seed, hbox, overfull, profilo, ramo, root,
  GUI, report, sessione, id). I nomi nel codice in inglese.
- In QML: colori e misure solo da `Theme`; un testo ha sempre una larghezza (`TxLabel`); ciò che scorre è fermo ai bordi
  (`TxScroll`, `boundsBehavior: Flickable.StopAtBounds`); ciò che ha un tasto non sta nei menu (ha uno `Shortcut`).
- Un caso limite di SPEC marcato [C] vale solo se la sua condizione c'è ancora: quelli tenuti hanno una prova
  (regola delle lezioni, «Ferma» sulla famiglia del processo, `.aux` troncato).

## Cronologia

`Workspace::undo/redo/goToStep` chiamano `tx::History`; `Workspace::checkpoint(titolo)` registra un passo (prima di un
lavoro le modifiche a mano, dopo il lavoro con il suo nome). In QML ⌘Z passa da `ProjectView.undo()`: prima il testo
dell'editor finché la sua pila ne ha, poi la cronologia (salvando prima le schede). Dopo un salto `filesChanged` fa
rileggere gli editor senza modifiche in sospeso; una rilettura svuota la pila del testo.

## Prove senza mani

`--gesti`, separati da «;»: `apri FILE [RIGA]`, `compila`, `tutto`, `salta` (⌘J), `sorgente PAGINA X Y`, `divisore 0|1|2`,
`avvisi`, `progetto chiuso`, `annulla`, `ripeti`, `passo TITOLO`, `scrivi FILE TESTO`, `fatta FILE`, `impostazioni`,
`preferenze [N]`, `stile FILE.sty` (importa), `usa-stile NOME`, `converti` (tutta la coda), `converti finestra`, `apri rivedi:dubbi|rivedi:correzioni`, `scegli LEZIONE DUBBIO N`, `apri rivedi:figure`, `figura NOME ridisegna|ritaglia`, `risolvi`, `impagina`, `apri rivedi:impaginazione`, `chiedi TESTO` (un messaggio all'agente: costa), `modo auto|chiedi|autonomo`, `rispondi si|no` (a una richiesta di conferma), `lezione N DATA FILE…` (crea una lezione), `nuova-lezione` (apre la finestra), `materiale FILE…` (fa il piano dello smistamento), `avvia-materiale`, `versioni` (la linguetta Version control), `apri diff:FILE|commit:HASH`, `rinomina FILE NOME`, `console`, `argomenti` (propone la scaletta), `applica-argomenti`, `per-data`, `salva-profilo NOME`, `nuovo CARTELLA [PROFILO]`, `correggo LEZIONE DUBBIO`, `decidi LEZIONE [DUBBIO…]` (costa), `tag [no]` (mostra o nasconde i tag di provenienza), `libreria STILE`, `avvia-piano`, `archivia`, `a-mano` (la prima mossa, a mano), `risolta`, `mossa STATO` (decide la mossa attuale), `annulla-mossa`, `elimina-impaginazioni`, `chiudi-scheda`, `inserisci TESTO`, `miniature`, `ramo LEZIONE` (apre la lezione nell'albero), `pannello PANNELLO LEZIONE`, `cita` (⌘L), `allega FILE`, `foto editor|pdf|finestra`, `invia`, `storico`, `chat`, `attivita`, `versione si|no`, `fonti-lezione FILE…`. Ogni gesto parte quando niente lavora; poi la foto.

## Regole e stile (SPEC §4)

- **Regole editoriali**: otto sezioni neutre rispetto allo stile (parlano di ruoli, «un'osservazione», non di nomi di
  ambienti). Le Preferenze le mostrano impaginate, le fanno modificare e riportare al predefinito; quelle cambiate
  stanno in `~/Library/Application Support/Texfix/Texfix (nuovo)/regole/<id>.md`.
- **Stile**: `Workspace::style()` è la descrizione dello stile del progetto. Da lì vengono i colori di begin ed end
  nell'editor (`Theme::readable` li rende leggibili sullo sfondo) e, con `Style::forPrompt`, le convenzioni per i
  prompt. Cambiare stile (`styleswitch::apply`) è un passo solo della cronologia.
- I prompt si compongono da tre parti: strutturale (nel codice), `rules::forPrompt`, `Style::forPrompt`.

## Stato

Che cosa c'è, sezione per sezione di `SPEC.md`, e che cosa manca. «Provato» vuol dire: con le prove del motore, o con
gesti e foto, o con il modello vero su un progetto di prova. Niente è stato provato con mouse e tastiera veri.

**§3.1 Progetti, §3.2 Editor, PDF, compilazione, §3.14 Cronologia, §3.15 Impostazioni.** Fatti. Il primo passo della
cronologia è «Modifiche fuori da Texfix» se i file sono cambiati dalla chiusura (`build/stato-alla-chiusura`).

**§3.3 Albero.** Gruppi: Lavori, Lezioni (o Argomenti), Da convertire (con «Smista in lezioni»), Documento, Fonti senza
lezione, Da consultare (ci si trascinano file: `Workspace::addDocuments`), Esiti, Immagini (con la lezione che le usa).
Una lezione si apre con la freccia: dentro le righe Dubbi, Correzioni, Impaginazione, Figure con il conto (un clic
apre il pannello su quella lezione: `showLesson`) e le sue fonti. Le fonti di una lezione sono quelle che i suoi
marcatori nominano, più la cartella `fonti/usate/lezione N/`; i nomi si confrontano con `files::bare`, l'unico posto
con quella regola. Il doppio clic apre con un'altra app ciò che non è testo.

**§3.4 Nuova lezione.** Numero e data proposti, «Prendi dal file», l'avviso se le fonti dicono altro. Le fonti sono
miniature (trascinate, dal disco, dalla coda); un clic apre il menu: apri, dai importanza, togli. Una fonte si
converte. Più scansioni o PDF: una chiamata leggera (`newlesson::overlap`) dice se si sovrappongono; se no si uniscono
in `fonti/coda/lezione N.pdf` nell'ordine giusto (`unite`: `pdfunite`, `sips` per le immagini) e si convertono, con una
riga in testa all'esito; se sì, o se sono di tipo diverso, le fonde l'agente con un piano (le fonti importanti decidono
dove le altre dicono altro). Versioni delle fonti (`versionOf`, `replaceVersion`): un file identico a una fonte usata
non si aggiunge; uno diverso con lo stesso nome fa chiedere se è la versione nuova (`versionAsked`): la vecchia va in
`precedenti/` con la data, la nuova prende il suo posto, una copia va in coda e l'agente porta nella lezione ciò che è
cambiato. A mano: «Carica una versione nuova…» nel menu di una fonte usata.

**§3.5 Aggiungi materiale.** Il piano dello smistamento, «Avvia», «Rivedi piano», la coda delle procedure dell'agente.

**§3.6 Converti.** Scansioni, PDF, immagini, documenti di testo, `.tex` di altri autori. Stima, esito corto con
«Resoconto completo» (la Console), «Ferma», «Riprendi». Dopo le figure sistema errori e righe, poi impagina. Le figure
dei `.tex` (`figures::authored`): con «Tieni» il codice dell'autore resta, asciugato (via commenti e righe vuote) solo se la resa è identica, e
un disegno più largo della colonna si restringe; con «Ridisegna» il disegnatore lo rifà guardando la resa del codice. Le lezioni di un `.tex` che il corso ha
già: una riga le conta e «Fai il piano» le affida all'agente. Se c'è un piano di materiale da approvare, Converti apre
quello. «Ridisegna» per i `.tex` è provato con il modello.

**§3.7 Rivedi dubbi e correzioni.** Scegli, «Correggo io», «Decidi tu», «Fai decidere al modello tutti i dubbi» (anche
dal menu della lezione), «Ricorda come regola del progetto», riepilogo, «Riapri», «Annulla la modifica», «Riapri
tutta». Domande, letture, aggiunte e il prima e dopo si vedono come nel PDF (`core/Snippets`, `Workspace::snippet`):
intanto il testo; ciò che non compila resta testo. In Rivedi correzioni ci sono anche errori e righe fuori misura
dell'ultima compilazione, con «Sistema» (`fixAll(lezione)`). Dopo «Riorganizza per argomenti» i dubbi seguono il loro
passo nel file che lo contiene. La lista si rilegge ma cambia solo ciò che è cambiato (`ReviewView.reload`: una riga
uguale a prima resta la stessa, e le schede restano finché il numero delle righe non cambia).

**§3.8 Figure.** Registro unico, «Ridisegna», «Ritaglia dalla fonte», Rivedi figure, «Ricontrolla con le fonti»
(l'agente rilegge le fonti e propone un piano).

**§3.9 Impaginazione.** L'impaginazione non usa il modello: ogni mossa si misura compilando la lezione (una lezione con molte figure
costa 7-10 secondi a compilazione, ed è lì che va il tempo). L'ordine conta: in ogni lezione c'è una mossa attuale, la prima ancora da rivedere
(`role` in `layoutItems`: chiusa, attuale, attesa). Quelle prima sono oscurate e non si toccano più; quelle dopo aspettano.
Per cambiarne una già decisa si torna indietro una alla volta: la × che compare passando sulla scheda attuale annulla la
scelta di prima (`layout::reopen`: la mossa torna com'era quando il programma l'ha lasciata, da `origine` nel registro).
Se una decisione cambia la lezione le mosse non decise che seguono si tolgono e si ricalcolano (`layout::dropAfter`, poi
`layout::lesson`). Ciò che il programma non risolve (un a capo non basta, più lungo di una colonna, un grande vuoto) è
«segnalata», senza una riga sua: è da rivedere come le altre, con Accetta, a mano o all'agente, e senza «Lascia com'era». «Elimina le impaginazioni» (`layout::reset`, con
conferma) toglie tutte le mosse dalle lezioni non fatte per farle rifare. Rivedi impaginazione prende tutta la finestra: i
pannelli si chiudono e tornano com'erano quando la si lascia (`ProjectView.syncReview`). È fatta come nella vecchia Texfix
(`qml/LayoutView.qml`): la panoramica, lezione per lezione, con una scheda piccola per mossa (stato, nome del blocco, una
pagina) e «Accetta le restanti»; un clic apre la scheda, con le pagine «Com'era» e «Com'è venuta» a sinistra e a destra le
scelte colorate (Accetta A, all'agente S, a mano M, com'era L; frecce per la scheda prima e dopo, Esc per la panoramica).
Nella modifica a mano le prove non restano finché non si preme «Applica alla lezione»: «Chiudi senza applicare» (o chiudere
la lezione) rimette la lezione e il registro com'erano (`Workspace::restoreManual`). Una lezione senza marcatori dei blocchi (scritta a mano o portata da fuori) resta da impaginare, e
l'impaginazione glieli aggiunge. La revisione elenca anche le lezioni ancora da impaginare. Un clic sul testo di una mossa
apre le pagine in piccolo del prima e del dopo (`layout::snap`, in `build/impaginazione/<lezione>/`), con il contorno del
blocco rosso com'era e verde com'è venuto. Una mossa che fa uscire qualcosa dalla colonna non resta. Un'unità spezzata fra due pagine (a pagina nuova, o spezzata fra due paragrafi); un titolo
rimasto in fondo senza il suo testo (a pagina nuova con lui); un grande vuoto prima di un'unità che non ci stava (una figura si
rimpicciolisce quanto basta, `\scalebox{…}{% impaginazione` … `}% impaginazione` attorno al disegno, fino al 55%; se non basta resta
dov'è con un a capo e il vuoto è voluto; il resto si segnala); ciò che non si spezza (una figura, un riquadro intero) ed è passato oltre
da solo lasciando poco vuoto (fra il 2% e il 25%) prende un a capo che fissa il punto, di solito «invariata»; una formula rimasta in fondo senza la frase che la commenta (passa oltre con il paragrafo che la introduce: da una riga vuota, o da dopo la formula di prima se il paragrafo nel sorgente continua, e allora la riga è `\par\newpage\noindent % impaginazione`) e un elenco che lascia
una voce sola dall'altra parte (la penultima raggiunge l'ultima, o l'elenco passa oltre con la sua frase): `breakBefore`, un a capo dentro il blocco
(`\par\newpage % impaginazione` nel testo, il comando dello stile in un riquadro), se il vuoto è al più il 25%. Qualunque unità passata oltre
lasciando un grande vuoto prende un a capo e il vuoto è voluto. Resta segnalato solo ciò che nessuna mossa sistema: un riquadro senza una
cucitura dove spezzarlo, una formula o un elenco che a capo lascerebbero troppo vuoto, un titolo che un a capo non riunisce al suo testo. Un'unità che finisce con poche righe sulla pagina dopo resta intera allungando la pagina
(`\enlargethispage`, su una colonna sola). Ogni mossa si accetta, si lascia com'era, si sistema a mano o si manda
all'agente. A mano (`ProjectView.startManual`): Progetto e chat si chiudono, la lezione si apre al blocco accanto alla
prova, che è della lezione sola (la prima si compila da sola, poi «Prova» o ⌘B). La striscia in alto dice che cosa c'è
da sistemare e ha «Vai alla mossa» (nell'editor e nella prova), lo scambio «Con la mossa · Senza la mossa», i comandi
pronti da inserire alla riga del cursore (`\newpage`, il comando dello stile per spezzare, `\vspace{-2mm}`). ⌘J porta
dal cursore alla prova (se la riga non lascia segno, una vicina). «Risolta», «Torna alla revisione» o
chiudere la lezione dall'editor rimettono i pannelli com'erano e ricompilano il documento. Una mossa si decide anche
mentre un lavoro gira, purché non stia lavorando su quella lezione. Righe sole in cima o in fondo le evita `texfix.sty`. Con poco spazio (finestra sotto i 900 px) la prova va sotto l'editor (`ProjectView.stacked`).

I tre giri (unità a cavallo, titoli, vuoti e formule) si ripetono finché uno non cambia più niente, al più cinque volte: una figura
rimpicciolita o un a capo spostano ciò che segue, e un riquadro può finire a cavallo dopo. Un titolo che comincia nell'ultimo 13% della
colonna (`NEAR_END`) passa oltre con il suo testo, anche se due righe gli stavano sotto. Ogni prova lascia il PDF com'era con la prova: chi misura dopo ricompila prima (`breakInside` lo fa da sé, e se l'ultima cucitura non regge
prova le due prima). Le misure: un'unità che non si spezza sta dove sta il segno più alto delle sue righe (`tallest`: la prima riga lascia un segno
in fondo alla colonna di prima, lo spazio che la precede); i segni fuori dall'area del testo non contano (`spotOf`: una riga su cui
la pagina si chiude si prende intestazione e piè di pagina).

**§3.10 Errori e righe.** Gli errori al modello. Le righe: prima una formula in display troppo larga spezzata a ogni
segno di relazione o a un + (`fix::splitDisplay`), poi il paragrafo spezzato a una fine di frase, poi il modello.

**§3.11 Riorganizza, §3.12 Lavori, §3.16 Version control.** Fatti. Version control: differenze affiancate (in colonna
con poco spazio o a richiesta), scelta dei file del commit con la spunta.

**§3.13 Agente e chat.** Streaming, righe degli strumenti che si aprono (che cosa ha chiesto), Sonnet o Opus, tre modi.
Allegati con miniature (trascinati o scelti; copiati in `salvataggi/chat/allegati/`), foto di un pezzo della finestra,
⌘L porta la selezione nel messaggio, storico con elimina a due clic, una chat con il suo nome per ogni procedura
(`Chat::startProcedure`). Se la chat di prima non si riprende, riparte con la conversazione nel messaggio. L'agente è
chiuso nel progetto in tutti e tre i modi: ogni richiesta di permesso passa da `conferma`, che nega da sé ciò che sta
fuori dalla cartella (provato con il modello vero); «Autonomo» consente da sé ciò che sta dentro.

**§3.17 Attività.** Nel pannello in basso, accanto ad Avvisi e Console: una riga per la compilazione, il lavoro, l'agente,
le procedure in attesa, il lavoro da riprendere, i lavori finiti da poco.

**§3.18 Tag di provenienza.** Li disegna la vista del PDF, dai marcatori dei blocchi; non sono nel PDF su disco (scelta
confermata dall'utente il 15/11/2026).

**§4 Regole e stile.** Regole editoriali nelle Preferenze; stile facoltativo. All'importazione il modello legge il `.sty`
e propone ruoli, note, macro e convenzioni (`styleswitch::propose`), poi l'utente conferma. Al cambio di stile gli
ambienti senza corrispondente sono una domanda (`orphans`, `qml/StyleMatchSheet.qml`); le macro dello stile di prima
cambiano nome se il nuovo ne ha una con lo stesso significato, altrimenti la loro definizione passa in `texfix.sty`
(`macro_portate` in `progetto.json`). Se il `.sty` del progetto cambia (l'agente, o l'utente che lo salva) la
descrizione lo segue (`refresh`) e un ambiente nuovo è una domanda. La libreria: gli stili di Texfix (`:/stili/`) e
quelli dell'utente (Preferenze › Stili; fra i dati del programma, `stili/`). Profili di partenza.
A confronto con la vecchia app sulle stesse pagine (`../prova-texfix`, src4.pdf e src5.pdf): stesse famiglie di
riquadri, stesso numero di definizioni e di figure; la nuova dà titoli alle sezioni, mette in un riquadro
un'osservazione che la vecchia lasciava in prosa, e non va a capo nel sorgente. Simili, non uguali.

**§5 Prove.** `./build.sh prove` (46 prove del motore), `./build.sh misure` (i tempi di ciò che l'interfaccia rifà
spesso, contro `tests/misure.txt`; `--scrivi` li riscrive). Dei riferimenti della vecchia app sono portate le formule in display vere (`tests/dati/display.json`), per la mossa
sulle formule; il resto dipende da prompt e stile, che qui sono cambiati per scelta.

Attenzione in QML: una proprietà non può chiamarsi `on…` con la maiuscola dopo (`onStyles`): viene presa per un
gestore di segnale e vale sempre falso.
