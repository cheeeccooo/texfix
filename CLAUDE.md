# Texfix (nuovo)

La riscrittura di Texfix come app nata app. Che cosa deve fare: `SPEC.md`. La vecchia app (`../texfix`) è il riferimento
quando un dettaglio manca: si legge il suo codice prima di scrivere il modulo corrispondente (SPEC §8), non lo si copia.
Questo file descrive il codice com'è: si aggiorna insieme al codice.

## Comandi

```sh
./build.sh                    # configura (la prima volta) e compila in build/, al massimo 4 processi
./build.sh prove              # compila e lancia le prove del motore (build/texfix_tests)
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
| `core/Review` | ciò che resta da decidere: i dubbi (`salvataggi/dubbi/`: scegli una lettura, riapri, annulla la modifica) e le aggiunte e correzioni dichiarate (`salvataggi/correzioni.json`: tieni, togli, rimetti); i conti per lezione |
| `core/Figures` | il registro delle figure (`salvataggi/figure.json`); il segno `% img <lezione>-NN` nella lezione; «Ridisegna» (TikZ: disegno, prova con `pdflatex`, revisione sulla resa) e «Ritaglia dalla fonte» (`imgs/<nome>.png`); i pacchetti chiesti dai disegni vanno in `progetto.json` e da lì in `texfix.sty` |
| `core/Fix` | errori e righe fuori misura di una lezione: gli errori al modello (sostituzioni esatte), le righe prima spezzando il paragrafo a una fine di frase poi con varianti del modello; ogni mossa resta solo se, compilata (`Compiler::compileSync`), migliora |
| `core/Layout` | l'impaginazione, con qualunque stile: dove cade ogni blocco nel PDF (SyncTeX, e l'area del testo scritta nel log da `texfix.sty`); un'unità a cavallo di due pagine va a pagina nuova (`\newpage % impaginazione` prima del marcatore) se il vuoto è al più il 25%, altrimenti la mossa resta proposta; registro `salvataggi/impaginazione.json` |
| `core/AgentTools` | gli strumenti che l'app dà all'agente (`stato_progetto`, `compila_lezione`, `compila_documento`, `dubbi`, `impagina`), il protocollo a righe (MCP) e il prompt di sistema dell'agente |
| `core/Plans` | i lavori con un piano: `salvataggi/lavori/<nome>/piano.json` (voci con spunta e istruzioni, domande, commento, esito); l'agente consegna piano ed esito con gli strumenti `consegna_piano` e `consegna_esito`; l'archivio in `salvataggi/esiti/` |
| `core/NewLesson` | numero e data proposti o letti dai nomi e dal testo delle fonti (senza modello); i file portati in `fonti/coda`; `settle` chiude una lezione scritta dall'agente (documento, marcatori, fonti usate: lo strumento `chiudi_lezione`); i messaggi delle procedure di fusione e integrazione |
| `core/Sort` | Aggiungi materiale: data e argomento di ogni pagina (una chiamata per file, tenuta in `build/lavori/smistamento/`), poi una chiamata con le lezioni del corso; il piano (tipo «materiale») e `prepare`, che divide i PDF in pezzi e dice che cosa si converte e che cosa passa dall'agente |
| `core/Git` | il version control dell'utente: modifiche, differenze, commit di tutto, cronologia (chiama `git`) |
| `core/Reorganize` | Riorganizza: le sezioni del corso (fra un `\subsection` e il successivo), gli argomenti proposti dal modello, la scrittura di un file per argomento e il ritorno a un file per lezione, tutti e due del programma; resta uguale il contenuto riga per riga, o non si tocca niente |
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
- Mai provare su un documento vero. Le prove dell'interfaccia usano `TEXFIX_PREFERENZE=<file>` (preferenze separate).
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
`preferenze [N]`, `stile FILE.sty` (importa), `usa-stile NOME`, `converti` (tutta la coda), `converti finestra`, `apri rivedi:dubbi|rivedi:correzioni`, `scegli LEZIONE DUBBIO N`, `apri rivedi:figure`, `figura NOME ridisegna|ritaglia`, `risolvi`, `impagina`, `apri rivedi:impaginazione`, `chiedi TESTO` (un messaggio all'agente: costa), `modo auto|chiedi|autonomo`, `rispondi si|no` (a una richiesta di conferma), `lezione N DATA FILE…` (crea una lezione), `nuova-lezione` (apre la finestra), `materiale FILE…` (fa il piano dello smistamento), `avvia-materiale`, `versioni` (la linguetta Version control), `apri diff:FILE|commit:HASH`, `rinomina FILE NOME`, `console`, `argomenti` (propone la scaletta), `applica-argomenti`, `per-data`. Ogni gesto parte quando niente lavora; poi la foto.

## Regole e stile (SPEC §4)

- **Regole editoriali**: otto sezioni neutre rispetto allo stile (parlano di ruoli, «un'osservazione», non di nomi di
  ambienti). Le Preferenze le mostrano impaginate, le fanno modificare e riportare al predefinito; quelle cambiate
  stanno in `~/Library/Application Support/Texfix/Texfix (nuovo)/regole/<id>.md`.
- **Stile**: `Workspace::style()` è la descrizione dello stile del progetto. Da lì vengono i colori di begin ed end
  nell'editor (`Theme::readable` li rende leggibili sullo sfondo) e, con `Style::forPrompt`, le convenzioni per i
  prompt. Cambiare stile (`styleswitch::apply`) è un passo solo della cronologia.
- I prompt si comporranno da tre parti: strutturale (nel codice), `rules::forPrompt`, `Style::forPrompt`.

## Stato

Fatto: §3.1; §3.2 tranne «Risolvi tutto» e il menu contestuale dei file; di §3.3 le lezioni con il pallino e i gruppi;
§3.14 (manca «Modifiche fuori da Texfix»); §3.15 (impostazioni, regole del progetto, «Fatta»); di §4 le regole
editoriali nelle Preferenze, lo stile facoltativo con importazione, domande sui ruoli e cambio di stile per ruoli.

Di §4 manca: i ruoli proposti dal modello (oggi dal nome), le domande per gli ambienti senza corrispondente al cambio
di stile (oggi restano com'erano e lo si dice), le macro al cambio di stile, la libreria di stili, i profili di
partenza, `styles` ripulito e descritto (nel suo repository), l'agente che modifica lo stile.

Di §3.6 c'è: Converti per scansioni, PDF, immagini e documenti di testo, con stima, esito corto, «Ferma» e «Riprendi»
(il lavoro gira in un thread del programma: `Workspace::convert`). Le figure si ridisegnano o si ritagliano dentro Converti, insieme (§3.8); «Rivedi figure» (`qml/FiguresView.qml`,
`Workspace::redoFigure`) le mostra tutte e le rifà una alla volta. Dopo le figure Converti sistema errori e righe della lezione (`fix::lesson`); «Risolvi tutto» negli Avvisi lo fa
sulle lezioni non fatte (`Workspace::fixAll`). Un disegno più largo della colonna si restringe con `\resizebox`.
Manca: le fonti `.tex`, «Ricontrolla con le fonti», le mosse sui display troppo larghi, l'impaginazione, il titolo per le fonti di testo, il dubbio su numero e data,
la Console con il registro.

Di §3.7 c'è: Rivedi dubbi e Rivedi correzioni come schede (`qml/ReviewView.qml`, `Workspace::reviewItems`), con la
pagina della fonte, il riepilogo, riapri e annulla la modifica, i tasti nella barra solo se serve. Manca: «Correggo
io», «Decidi tu», «Ricorda come regola», la resa compilata dei pezzi, la lista che si aggiorna riga per riga.

Di §3.9 c'è: l'unità spezzata fra due pagine, «Rivedi impaginazione» (`qml/LayoutView.qml`), l'impaginazione da sola
dopo Converti, righe sole in cima o in fondo evitate da `texfix.sty`. Manca tutto il resto del §3.9: titoli in fondo
alla pagina, formule e frasi, figure lontane, grandi vuoti, le altre mosse (spezzare in un punto scelto, spazio,
misura delle figure), mandare all'agente, risolvere a mano con le prove.

Di §3.13 c'è: il pannello chat (`qml/ChatPane.qml`) con streaming, righe degli strumenti, Sonnet o Opus, ⌘↩, «Ferma»,
l'ultima chat che torna all'apertura; il turno dell'agente è un passo della cronologia. Tre modi, come in Claude Code (`Chat::mode`, per tutti i progetti): Automatico (predefinito, `--permission-mode auto`),
Chiedi conferma (`manual`), Autonomo (`bypassPermissions`); nei primi due ciò che vuole una conferma arriva alla chat
come scheda «Consenti · Nega» (`--permission-prompt-tool mcp__texfix__conferma`, `Chat::answer`).
Di §3.12 c'è: il piano come documento fra le schede (`qml/PlanView.qml`), «Avvia» o «Rivedi piano», «Butta il piano»
con conferma, l'esito, «Chiudi e archivia», il gruppo «Lavori» nell'albero.
Manca: allegati e screenshot, ⌘L, lo storico con elimina, le procedure dei tasti (fusione delle fonti, integrazione,
«Decidi tu», Riorganizza) con la loro chat, il gruppo «Esiti».

Di §3.4 c'è: «＋ Nuova lezione» (`qml/NewLessonSheet.qml`): numero e data proposti, «Prendi dal file», l'avviso se le
fonti dicono altro (un secondo clic tiene i valori), una fonte → Converti con numero e data scelti (e un dubbio sulla
riga `\lezione` se la scansione dice altro), più fonti → l'agente le fonde con un piano; «Aggiungi fonti a questa
lezione…» nel menu della lezione. Di §3.5 c'è: «＋ Aggiungi materiale» e il trascinamento sull'albero portano i file in coda e fanno il piano dello
smistamento (`Workspace::sortMaterial`); «Avvia» (`startSortPlan`) converte insieme le lezioni nuove fatte di un file
solo e senza istruzioni, e mette in coda le procedure dell'agente per il resto (`m_agentQueue`, una alla volta, quando
niente lavora e nessun piano è aperto); «Rivedi piano» rifà solo l'ultima chiamata.
Manca: le miniature delle fonti, la chiamata leggera che dice se più fonti si sovrappongono (oggi vanno sempre
all'agente), le versioni delle fonti, «Smista in lezioni» sul gruppo Da convertire.

§3.16: a sinistra due linguette, Progetto e Version control con il numero delle modifiche (`qml/GitPanel.qml`):
Inizializza, commit di tutto con conferma, cronologia; un clic apre le differenze in una scheda (`qml/DiffView.qml`).
Manca: le differenze affiancate (oggi righe rosse e verdi in colonna), scegliere quali file mettere nel commit.

Menu contestuale dei file (tasto destro nell'albero; `tx::fileops` in `core/Project`): apri, apri e compila, app
predefinita, Finder, copia percorso o nome, rinomina (il documento segue la lezione, la scheda segue il file), duplica,
Cestino (⌘Z lo riporta). Nel pannello in basso due linguette: Avvisi e Console (`Workspace::console`, il registro dei
messaggi e dei lavori).

§3.11: Progetto › «Riorganizza per argomenti…» (il modello propone, `qml/OutlineSheet.qml` fa sistemare la scaletta:
rinomina, ordina, sposta, argomento nuovo) e «Riorganizza per data di lezione» (attivo solo se i file sono per
argomenti; senza modello). Le lezioni com'erano stanno in `progetto.json` (`argomenti`); in modo argomenti l'albero
dice «Argomenti».

Da fare, nell'ordine previsto: il resto dell'impaginazione; i piani dei lavori (§3.12); agente e chat con
strumenti (§3.13); Riorganizza; Version control; menu contestuale dei file.
