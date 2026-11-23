#pragma once
#include "core/Compiler.h"
#include "core/Convert.h"
#include "core/History.h"
#include "core/Run.h"
#include "core/Settings.h"
#include "core/Style.h"
#include "core/Project.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QThread>
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <memory>

// Il progetto aperto, per la finestra: un modello solo in memoria, da cui QML legge e a cui chiede le azioni.
class Workspace : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString root READ root NOTIFY projectChanged)
    Q_PROPERTY(QString name READ name NOTIFY projectChanged)
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    // Le lezioni del documento: file, number, date, title, state (idle | good | warn | bad), detail; rows (name, panel,
    // detail: ciò che c'è da rivedere) e sources (file, name: le sue fonti).| good | warn | bad), detail.
    Q_PROPERTY(QVariantList lessons READ lessons NOTIFY treeChanged)
    // Gli altri file del documento: main.tex, texfix.sty e i .tex di src/ che il main non richiama.
    Q_PROPERTY(QVariantList documentFiles READ documentFiles NOTIFY treeChanged)
    // Le fonti da convertire (fonti/coda), i documenti da consultare (docs/), le immagini (imgs/): file, name.
    Q_PROPERTY(QVariantList queue READ queue NOTIFY treeChanged)
    Q_PROPERTY(QVariantList documents READ documents NOTIFY treeChanged)
    Q_PROPERTY(QVariantList images READ images NOTIFY treeChanged)
    Q_PROPERTY(QVariantList strays READ strays NOTIFY treeChanged)        // le fonti usate che nessuna lezione nomina
    Q_PROPERTY(QVariantList outcomes READ outcomes NOTIFY treeChanged)    // gli esiti archiviati: file, name (il titolo), detail (quando)
    Q_PROPERTY(bool compiling READ compiling NOTIFY compilingChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    // Gli avvisi dell'ultima compilazione: kind (error | wide | loose), file, line, text, detail.
    Q_PROPERTY(QVariantList issues READ issues NOTIFY issuesChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY issuesChanged)
    Q_PROPERTY(QString pdf READ pdf NOTIFY pdfChanged)
    // Quanto resta da decidere nelle lezioni non fatte: i tasti «Rivedi» ci sono solo se è più di zero.
    Q_PROPERTY(int openDoubts READ openDoubts NOTIFY treeChanged)
    Q_PROPERTY(int openDeclared READ openDeclared NOTIFY treeChanged)
    Q_PROPERTY(int openFigures READ openFigures NOTIFY treeChanged)       // le figure ancora da inserire
    Q_PROPERTY(int openLayout READ openLayout NOTIFY treeChanged)         // mosse da rivedere e lezioni da impaginare
    // Le lezioni non fatte che all'ultima compilazione avevano errori: file, number, errors, detail («1 errore»). Con
    // loro c'è il tasto «Risolvi errori».
    Q_PROPERTY(QVariantList errorLessons READ errorLessons NOTIFY treeChanged)
    // Il lavoro in corso (Converti…): uno alla volta. busy: un lavoro o una compilazione.
    Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobChanged)
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY jobProgressChanged)
    Q_PROPERTY(QString jobLabel READ jobLabel NOTIFY jobProgressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool resumable READ resumable NOTIFY jobChanged)           // un lavoro interrotto, da riprendere
    // Per il pannello Attività: i lavori finiti da poco (title, when, stopped).
    Q_PROPERTY(QVariantList recentJobs READ recentJobs NOTIFY jobChanged)
    // Il lavoro in pausa per il limite di utilizzo: fino a quando («HH:mm», o con il giorno); vuoto se non è in pausa.
    Q_PROPERTY(QString pausedUntil READ pausedUntil NOTIFY jobProgressChanged)
    // L'utilizzo di Claude (core/Usage, dalle chiamate fatte): known; hours e week {used (0-100), resets («alle 18:40»),
    // status}; ago (minuti dall'ultima chiamata); blocked (fino a quando, se il limite è raggiunto); e mentre si lavora
    // runsOut: a questo ritmo quando arriva al limite («fra circa 25 minuti»), vuoto se non si sa o se si azzera prima.
    Q_PROPERTY(QVariantMap usage READ usage NOTIFY usageChanged)
    Q_PROPERTY(QVariantList waitingLimit READ waitingLimit NOTIFY waitingLimitChanged)   // [{title, until}]: aspettano il limite
    // La connessione: offline, le chiamate del lavoro aspettano che torni; pausedForConnection, il lavoro si ferma perché
    // non è tornata (pausedUntil è allora adesso: si riprende quando c'è).
    Q_PROPERTY(bool offline READ offline NOTIFY jobProgressChanged)
    Q_PROPERTY(bool pausedForConnection READ pausedForConnection NOTIFY jobProgressChanged)
    Q_PROPERTY(QStringList jobLog READ jobLog NOTIFY jobLogChanged)       // il registro del lavoro (Console)
    // Le regole del progetto, una per voce (Impostazioni del progetto).
    Q_PROPERTY(QStringList projectRules READ projectRules WRITE setProjectRules NOTIFY settingsChanged)
    Q_PROPERTY(QString styleName READ styleName NOTIFY styleChanged)
    Q_PROPERTY(QString breakCommand READ breakCommand NOTIFY styleChanged)   // come lo stile spezza un riquadro in un punto scelto

public:
    static Workspace *instance();
    static Workspace *create(QQmlEngine *, QJSEngine *);
    static QSettings settings();                       // TEXFIX_PREFERENZE=file: preferenze di prova, non quelle vere

    bool hasProject() const { return m_project.isValid(); }
    QString root() const { return m_project.root(); }
    QString name() const { return m_project.name(); }
    QVariantList recents() const;
    QVariantList lessons() const { return m_lessons; }
    QVariantList documentFiles() const { return m_documentFiles; }
    QVariantList queue() const { return m_queue; }
    QVariantList documents() const { return m_documents; }
    QVariantList images() const { return m_images; }
    QVariantList outcomes() const { return m_outcomes; }
    QVariantList strays() const { return m_strays; }
    bool compiling() const { return m_compiler.isRunning(); }
    double progress() const { return m_progress; }
    QString progressLabel() const { return m_progressLabel; }
    QVariantList issues() const { return m_issues; }
    int errorCount() const { return m_errorCount; }
    QString pdf() const { return m_pdf; }
    int openDoubts() const { return m_openDoubts; }
    int openDeclared() const { return m_openDeclared; }
    int openFigures() const { return m_openFigures; }
    int openLayout() const { return m_openLayout; }
    QVariantList errorLessons() const;
    bool jobRunning() const { return m_jobThread != nullptr; }
    QString jobTitle() const { return m_jobTitle; }
    double jobProgress() const { return m_jobProgress; }
    QString jobLabel() const { return m_jobLabel; }
    bool busy() const { return compiling() || jobRunning() || m_agentRunning; }
    bool resumable() const;
    QStringList jobLog() const { return m_jobLog; }
    QVariantList recentJobs() const { return m_recent; }
    QString pausedUntil() const;
    QVariantMap usage() const;
    bool offline() const { return m_offline; }
    bool pausedForConnection() const { return m_pauseConnection; }
    QStringList projectRules() const { return m_settings.profile.extra; }
    void setProjectRules(const QStringList &rules);
    QString styleName() const { return m_style.name; }
    QString breakCommand() const { return m_style.breakCommand; }
    const tx::Style &style() const { return m_style; }
    const tx::Project &project() const { return m_project; }
    // L'agente sta lavorando: come un lavoro, niente altro parte e niente si annulla.
    void setAgentRunning(bool running);
    void agentTurnDone();
    const tx::Settings &settings_() const { return m_settings; }

    // Apre una cartella. «» se è aperta; «prepara» se non è ancora un progetto: allora il foglio dell'inizio
    // (ProfileSheet) chiede come, e newProject la prepara; altrimenti il perché non si apre.
    Q_INVOKABLE QString openFolder(const QString &path);
    // I file di una cartella che sta per diventare un progetto, con la proposta: [{file, dove}], dove «coda», «docs».
    Q_INVOKABLE QVariantList folderFiles(const QString &path) const;
    // Un progetto nuovo, per data o per argomento, anche da un profilo di partenza (vuoto: nessuno), in una cartella
    // vuota o con dei file: placement dice dove va ognuno (file → «coda», «docs», «lascia»; Project::create). I
    // profili: i nomi; saveProfile salva le scelte del progetto aperto (vuoto se è andata).
    Q_INVOKABLE QString newProject(const QString &path, const QString &profile = {}, bool byTopic = false, const QVariantMap &placement = {},
                                   const QString &style = {});   // style: uno della libreria (gli altri, «@…», li fa chi chiama)
    Q_INVOKABLE QStringList profiles() const;                              // i predefiniti, poi la libreria
    Q_INVOKABLE QStringList builtinProfiles() const;
    Q_INVOKABLE QStringList libraryProfiles() const;
    // La copia del profilo del progetto va nella libreria con quel nome (unico; replace per sostituirne uno dell'utente).
    Q_INVOKABLE QString saveProfile(const QString &name, bool replace = false);
    Q_INVOKABLE void removeProfile(const QString &name);
    Q_INVOKABLE QString useProfile(const QString &name);                   // il profilo diventa la copia del progetto
    Q_INVOKABLE QString importProfile(const QString &path);                // nella libreria, e il progetto lo usa
    Q_INVOKABLE QString exportProfile(const QString &path);
    Q_INVOKABLE QString importProfileToLibrary(const QString &path);       // solo nella libreria (Preferenze)
    Q_INVOKABLE QVariantList structuralSections() const;                   // ciò che Texfix fa sempre, in sola lettura
    // Lo stile del progetto: nella libreria con un nome suo (cambia nome anche nel progetto), o in una cartella.
    Q_INVOKABLE QString saveStyleToLibrary(const QString &name, bool replace = false);
    Q_INVOKABLE QString exportStyle(const QString &dir);
    // L'agente crea o migliora il profilo e lo stile in una chat sua (agentAsked); consegna una proposta, che l'utente
    // vede accanto a ciò che c'è e applica o scarta: proposals [{kind: profilo|stile, name, now, proposed, fresh}].
    Q_INVOKABLE void askAgent(const QString &kind);                        // crea-profilo, migliora-profilo, crea-stile, migliora-stile
    Q_PROPERTY(QVariantList proposals READ proposals NOTIFY proposalsChanged)
    QVariantList proposals() const { return m_proposals; }
    Q_INVOKABLE QString acceptProposal(const QString &kind);
    Q_INVOKABLE void discardProposal(const QString &kind);
    Q_PROPERTY(QString profileName READ profileName NOTIFY settingsChanged)
    Q_PROPERTY(bool styleModified READ styleModified NOTIFY settingsChanged)
    QString profileName() const { return m_settings.profile.name; }
    bool styleModified() const;
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE static QString localPath(const QUrl &url);

    Q_INVOKABLE QString read(const QString &file) const;                   // file relativo alla radice
    Q_INVOKABLE bool save(const QString &file, const QString &text);
    Q_INVOKABLE bool isLesson(const QString &file) const;
    Q_INVOKABLE void watchFile(const QString &file);                       // un file aperto: se cambia su disco, filesChanged
    Q_INVOKABLE void openExternally(const QString &file) const;            // con l'app predefinita

    // Il salto sorgente ↔ PDF. toPdf: ok, page (da 1), x, y, height in punti; ok è falso se il PDF mostrato non ha
    // quella riga o è più vecchio del file. toSource: ok, file, line.
    Q_INVOKABLE QVariantMap toPdf(const QString &file, int line) const;
    Q_INVOKABLE QVariantMap toSource(int page, qreal x, qreal y) const;

    // Compila una lezione da sola (un altro file: il documento) o tutto il documento.
    Q_INVOKABLE void compileFile(const QString &file);
    Q_INVOKABLE void compileDocument();
    Q_INVOKABLE void stop();

    // Rivedi dubbi e Rivedi correzioni. Le righe da mostrare, lezione per lezione: type (lesson | doubt | declared) e i
    // loro dati; con summary anche ciò che è già deciso e le lezioni fatte. Ogni decisione è un passo della cronologia.
    Q_INVOKABLE QVariantList reviewItems(const QString &section, bool summary) const;
    Q_INVOKABLE void chooseReading(const QString &lesson, const QString &id, int reading);
    Q_INVOKABLE void reopenDoubt(const QString &lesson, const QString &id, bool revert);
    // «Correggo io»: ricorda com'è la lezione; dopo la modifica a mano la scheda mostra il prima e il dopo, e
    // confirmDoubt chiude. «Decidi tu»: decideDoubts fa decidere al modello i dubbi dati (vuoto: tutti quelli aperti
    // della lezione), un lavoro; le sue letture aspettano la conferma. addProjectRule: «Ricorda come regola del progetto».
    Q_INVOKABLE void correctDoubt(const QString &lesson, const QString &id);
    Q_INVOKABLE void confirmDoubt(const QString &lesson, const QString &id);
    Q_INVOKABLE void decideDoubts(const QString &lesson, const QStringList &ids);
    Q_INVOKABLE void addProjectRule(const QString &rule);
    Q_INVOKABLE void keepDeclared(const QString &lesson, const QString &hash, bool keep);
    Q_INVOKABLE void keepAllDeclared(const QString &lesson);
    Q_INVOKABLE void dropDeclared(const QString &lesson, const QString &hash);
    Q_INVOKABLE void restoreDeclared(const QString &lesson, const QString &hash);
    // «Riapri tutta»: tutto ciò che in una lezione era deciso (section: dubbi | correzioni) torna da decidere.
    Q_INVOKABLE void reopenLesson(const QString &lesson, const QString &section);

    // Rivedi figure: tutte le figure del progetto, lezione per lezione (type lesson | figure). redoFigure la ridisegna
    // (how: ridisegna) o la ritaglia dalla fonte (ritaglia): un lavoro, un passo della cronologia.
    Q_INVOKABLE QVariantList figureItems() const;
    Q_INVOKABLE void redoFigure(const QString &id, const QString &how, const QString &instruction = {});
    // Rivedi figure: le figure scritte fuori da Converti (quante, e metterle nel registro); la resa e l'originale di
    // quelle che non li hanno ancora (un lavoro); accettare; tornare al disegno; ritagliare con riquadro e cancellino
    // (frazioni della pagina); la pagina della fonte, se è già resa.
    Q_INVOKABLE int unadoptedFigures() const;
    Q_INVOKABLE void adoptFigures();
    Q_INVOKABLE void renderFigures();
    Q_INVOKABLE void acceptFigure(const QString &id, bool accepted);
    Q_INVOKABLE void acceptAllFigures(const QString &lesson, bool accepted);
    Q_INVOKABLE void useDrawing(const QString &id);
    Q_INVOKABLE void cropFigure(const QString &id, const QVariantList &box, const QVariantList &erase);
    Q_INVOKABLE QString figurePage(const QString &id) const;
    // «Ricontrolla con le fonti»: l'agente rilegge le fonti e propone un piano.
    Q_INVOKABLE void recheckFigures();

    // «Risolvi tutto»: errori e righe fuori misura delle lezioni non fatte (prima le mosse che non costano, poi il
    // modello), poi compila il documento. Un lavoro, un passo della cronologia.
    // only: una lezione sola; errorsOnly: solo le lezioni con errori («Risolvi errori»), non quelle con sole righe.
    Q_INVOKABLE void fixAll(const QString &only = {}, bool errorsOnly = false);

    // Impaginazione. paginate impagina le lezioni non fatte che sono nuove o cambiate (un lavoro, un passo della
    // cronologia). layoutItems: le mosse, lezione per lezione (type lesson | move). decideMove: accettata | lasciata.
    Q_INVOKABLE void paginate();
    // Rimpagina una lezione cambiata dopo l'ultima impaginazione, dal punto cambiato in giù.
    Q_INVOKABLE void paginateLesson(const QString &file);
    // Rifà le pagine in piccolo del «dopo» fatte da un'altra lezione (Rivedi impaginazione lo chiede quando niente lavora).
    Q_INVOKABLE void refreshShots();
    Q_INVOKABLE QVariantList layoutItems() const;
    // redo: se la lezione cambia, le mosse non ancora decise che vengono dopo si ricalcolano (l'ordine conta).
    Q_INVOKABLE void decideMove(const QString &lesson, const QString &block, const QString &state, bool redo = true);
    // Dov'è la mossa di un blocco nella lezione (line) e se la sua riga c'è (present); showsLesson: il PDF mostrato è la
    // prova di quella lezione sola.
    // Elimina tutte le impaginazioni delle lezioni non fatte, per farle rifare: un passo della cronologia.
    Q_INVOKABLE void resetLayout();
    // Annulla la scelta su una mossa (dalla panoramica, a ritroso): ciò che segue si ricalcola.
    Q_INVOKABLE void reopenMove(const QString &lesson, const QString &block);
    void redoFollowing(const QString &lesson, const QString &block, bool retake = false);
    // «Accetta le restanti»: tutte le mosse fatte da sole di una lezione, in un colpo.
    Q_INVOKABLE void acceptRest(const QString &lesson);
    // «Chiudi senza applicare» nella modifica a mano: la lezione e il registro tornano com'erano all'inizio.
    Q_INVOKABLE void restoreManual(const QString &lesson, const QString &text, const QString &registry);
    Q_INVOKABLE void keepManual(const QString &lesson, const QString &block, const QString &text, const QString &registry);
    Q_INVOKABLE QVariantMap moveSpot(const QString &lesson, const QString &block) const;
    Q_INVOKABLE bool showsLesson(const QString &file) const;

    // I lavori con un piano. jobs: quelli aperti (id, title, state). plan: il piano com'è. Ogni modifica dell'utente è una
    // chiamata sua, salvata subito: la spunta o l'istruzione di una voce (key: fare, nota), la scelta di una sezione (il
    // valore), la risposta a una domanda, il commento.
    // planMessage: ciò che si dice all'agente per avviarlo (how: avvia) o rifarlo (rivedi). planReady: l'agente ha
    // consegnato un piano o un esito.
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY treeChanged)
public:
    QVariantList jobs() const { return m_jobs; }
    Q_INVOKABLE QVariantMap plan(const QString &id) const;
    Q_INVOKABLE void setPlanEntry(const QString &id, int section, int entry, const QString &key, const QVariant &value);
    Q_INVOKABLE void setPlanChoice(const QString &id, int section, const QString &field, const QString &value);   // field: scelta, intreccio
    Q_INVOKABLE void setPlanAnswer(const QString &id, int question, const QString &answer);
    Q_INVOKABLE void setPlanOrder(const QString &id, bool keep);   // l'ordine nuovo degli argomenti: tenerlo o no
    Q_INVOKABLE void setPlanComment(const QString &id, const QString &text);
    // il piano dei concetti: che cosa cambiare della scaletta proposta (rifà la mappa, come un commento)
    Q_INVOKABLE void setOutlineNote(const QString &id, const QString &text);
    // Il piano dei concetti: una voce di un elenco (comuni, destinazioni, versioni) e il suo campo (valore, fare, nota).
    Q_INVOKABLE void setPlanField(const QString &id, const QString &list, int index, const QString &key, const QVariant &value);
    Q_INVOKABLE bool planHasInstructions(const QString &id) const;
    Q_INVOKABLE QString planMessage(const QString &id, const QString &how);
    Q_INVOKABLE void discardPlan(const QString &id);
    Q_INVOKABLE void archivePlan(const QString &id);

    // Aggiungi materiale, la porta sola (SPEC §3.4). addMaterial porta dei file fra quelli in arrivo, e basta:
    // li riconosce «Smista» (sortMaterial), che ne fa il piano. `lesson`: la lezione (o l'argomento) a cui l'utente li ha
    // aggiunti, ricordata finché sono in arrivo (newlesson::chosenLessons). newVersion: un file che l'utente dice versione
    // nuova di una fonte usata (arriva con il suo nome, e il piano lo riconosce). sortMaterial riconosce ciò che è in
    // arrivo; se un lavoro gira aspetta la sua fine. startSortPlan avvia il lavoro intero
    // (core/Run, nella finestra «lavoro:<piano>»): le lezioni nuove si convertono e l'agente fa il resto, insieme; reviseSortPlan
    // rifà il piano con le istruzioni, le scelte e i ruoli dell'utente. Ciò che non si riconosce resta in arrivo con il
    // perché (il dettaglio della sua riga) e si riprova con sortMaterial.
    Q_INVOKABLE void addMaterial(const QStringList &paths, const QString &lesson = {});
    Q_INVOKABLE void newVersion(const QString &used, const QString &path);
    Q_INVOKABLE void addDocuments(const QStringList &paths);               // copia dei file fra quelli da consultare (docs/)
    Q_INVOKABLE static QStringList localPaths(const QList<QUrl> &urls);
    Q_INVOKABLE void sortMaterial(const QStringList &sources);
    Q_INVOKABLE void startSortPlan(const QString &id);
    Q_INVOKABLE void reviseSortPlan(const QString &id);
    Q_INVOKABLE void askSettings() { emit settingsAsked(); }
    // I concetti di ciò che è in arrivo e la mappa delle intersezioni (core/Concepts): un lavoro; il risultato in
    // build/lavori/concetti/ultima-mappa.json. Il piano dei concetti lo userà.
    Q_INVOKABLE void conceptSurvey(const QStringList &sources);

    // Version control: lo usa solo l'utente. changeCount: quante modifiche non salvate (nel titolo della linguetta).
    Q_PROPERTY(int changeCount READ changeCount NOTIFY treeChanged)
    Q_PROPERTY(bool hasRepository READ hasRepository NOTIFY treeChanged)
public:
    int changeCount() const { return m_changeCount; }
    bool hasRepository() const;
    Q_INVOKABLE QVariantList gitChanges() const;                           // file, state
    Q_INVOKABLE QVariantList gitLog() const;                               // hash, when, subject
    Q_INVOKABLE QString gitDiff(const QString &what) const;                // «diff:<file>» o «commit:<hash>»
    Q_INVOKABLE QString gitCommit(const QString &message, const QStringList &files = {});   // vuoto se è andata; files: solo quelli
    Q_INVOKABLE void gitInit();

    // Il menu contestuale dei file: ogni azione è un passo della cronologia.
    Q_INVOKABLE QString renameFile(const QString &file, const QString &newName);   // vuoto se è andata
    Q_INVOKABLE void duplicateFile(const QString &file);
    Q_INVOKABLE void trashFile(const QString &file);
    Q_INVOKABLE void revealFile(const QString &file) const;                // nel Finder
    Q_INVOKABLE void copyText(const QString &text) const;                  // negli appunti
    // Il registro: i messaggi e le righe dei lavori, per la Console.
    Q_PROPERTY(QStringList console READ console NOTIFY consoleChanged)
public:
    QStringList console() const { return m_console; }

    // Riorganizza. byTopic: i file sono per argomenti. proposeTopics chiede al modello gli argomenti (un lavoro) e
    // poi outlineReady con la scaletta: topics [{title, sections [{id, title, lesson}]}]. applyTopics la scrive;
    // backToLessons torna a un file per lezione. Ognuno è un passo della cronologia, poi si compila il documento.
    // setByTopic: l'organizzazione del progetto dalle impostazioni; un progetto senza lezioni cambia e basta, gli altri
    // passano da proposeTopics o da backToLessons.
    Q_PROPERTY(bool byTopic READ byTopic NOTIFY settingsChanged)
    // Il programma (o l'indice, la scaletta) del corso: un file in docs/ che la mappa dei concetti segue per l'ordine
    // degli argomenti. setProgram lo copia in docs/ se viene da fuori; vuoto lo toglie.
    Q_PROPERTY(QString program READ program NOTIFY settingsChanged)
public:
    bool byTopic() const { return m_settings.byTopic(); }
    QString program() const { return m_settings.program; }
    Q_INVOKABLE void setProgram(const QString &path);
    Q_INVOKABLE void proposeTopics();
    Q_INVOKABLE QString applyTopics(const QVariantList &topics);          // vuoto se è andata
    Q_INVOKABLE void backToLessons();
    Q_INVOKABLE void setByTopic(bool on);

    // I tag di provenienza sul PDF: da quale fonte viene ogni blocco. showTags li mostra o li nasconde (non sposta una
    // riga: nel documento non c'è niente); tags: page, y, mark, source, sourcePage, path; tagSources: la legenda
    // (mark, name, path, hidden); una sigla si nasconde da sola con setMarkHidden.
    Q_PROPERTY(bool showTags READ showTags WRITE setShowTags NOTIFY tagsChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QVariantList tagSources READ tagSources NOTIFY tagsChanged)
public:
    bool showTags() const { return m_showTags; }
    void setShowTags(bool show);
    QVariantList tags() const { return m_tags; }
    QVariantList tagSources() const;
    Q_INVOKABLE void setMarkHidden(const QString &mark, bool hidden);

    // Il lavoro di «Avvia» (core/Run), nella sua finestra («lavoro:<piano>», a tutta finestra come le revisioni). run: id,
    // title, state (in_corso | in_pausa: il limite, finisce ciò che è in corso | fermato | interrotto | finito), until
    // (quando si può riprendere dopo il limite: «domani alle 02:21»), sessionStart (ms, 0 se fermo), worked (ms lavorati prima), cost, estimate, progress, active (chiamate in corso), sites (i cantieri: key, kind, title, file, state,
    // phases [{name, state, done, total}], failure, outcome), calls (le chiamate: id, site, siteTitle, role, what, model,
    // state, started, seconds, cost, error, last), outcome (lines, cost, minutes), canUndo. Vuoto se non c'è un lavoro.
    // runCall: una chiamata per intero (system, parts [{text} | {image}], response, stream [{kind, text}], error).
    // resumeRun riprende un lavoro fermato (anche dal limite) o interrotto; undoRun annulla tutto il
    // lavoro (il progetto torna com'era prima di «Avvia»); closeRun lo archivia fra gli esiti e butta i registri.
    Q_PROPERTY(QVariantMap run READ run NOTIFY runChanged)
public:
    QVariantMap run() const;
    Q_INVOKABLE QVariantMap runCall(int id) const;
    Q_INVOKABLE void resumeRun();
    Q_INVOKABLE void undoRun();
    Q_INVOKABLE void closeRun();

    // La conversione, da «Avvia» di Aggiungi materiale: le fonti diventano lezioni (un passo solo della cronologia), poi
    // si compila il documento; alla fine outcomeReady con title, lines, cost, minutes. resume riprende un lavoro
    // interrotto senza rifare ciò che è fatto.
    void convert(const QStringList &sources, const QHash<QString, tx::Convert::Wanted> &wanted);
    Q_INVOKABLE void resume();
    // Il limite di utilizzo in ogni azione: un'azione con il modello chiesta quando il limite è raggiunto (limited), o
    // un lavoro che il limite ha fermato, aspetta la ripresa e riparte da sé (later). Attività la mostra con «Adesso»
    // (retryLater) e «Lascia stare» (dropLater). limited: vero se l'azione aspetta, e allora chi chiama non parte.
    // key: che cosa la distingue da un'altra con lo stesso titolo (la lezione dei dubbi, la figura…); la stessa due volte
    // aspetta una volta sola
    bool limited(const QString &title, std::function<void()> again, const QString &key = {});
    void later(const QString &title, std::function<void()> again, const QDateTime &until, const QString &key = {});
    QVariantList waitingLimit() const;
    Q_INVOKABLE void retryLater(int index);
    Q_INVOKABLE void dropLater(int index);

    // Le impostazioni del progetto: le voci sono scrittura, qualita, figure_scansioni, figure_tex, impaginazione.
    // Ogni scelta è salvata subito.
    Q_INVOKABLE QVariantList settingChoices(const QString &key) const;     // value, label
    Q_INVOKABLE QString setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QString &value);
    Q_INVOKABLE void setDone(const QString &lesson, bool done);            // «Fatta»

    // Lo stile del progetto. importStyle copia un .sty nel progetto e lo descrive: error, name, environments (name,
    // role, color) da confermare se non era già descritto (described). applyStyle passa a quello stile (vuoto:
    // nessuno) riscrivendo le lezioni secondo i ruoli: un passo solo della cronologia.
    Q_INVOKABLE QVariantMap importStyle(const QString &styPath);
    Q_INVOKABLE QStringList styleLibrary() const;                          // gli stili che Texfix ha con sé
    Q_INVOKABLE void useLibraryStyle(const QString &name);                 // lo porta nel progetto e lo applica
    Q_INVOKABLE QVariantList styleRoles() const;                           // value, label
    Q_INVOKABLE void setStyleRoles(const QString &name, const QVariantMap &roles);   // ambiente → ruolo
    Q_INVOKABLE void applyStyle(const QString &name);
    // Gli ambienti senza corrispondente nel nuovo stile sono una domanda (styleQuestions: name, role, options); la
    // risposta torna qui: ambiente di prima → ambiente del nuovo stile, o «=» per lasciarlo com'è.
    Q_INVOKABLE void applyStyleWith(const QString &name, const QVariantMap &choices);
    // Il modello legge lo stile importato e propone ruoli e convenzioni (un lavoro); poi styleProposed con gli ambienti
    // da confermare. styleFileChanged: il .sty del progetto è cambiato, la descrizione lo segue.
    Q_INVOKABLE void proposeStyle(const QString &name);
    void styleFileChanged();
    // La libreria degli stili, per le Preferenze: name, bundled (di Texfix: non si toglie).
    Q_INVOKABLE QVariantList libraryStyles() const;
    Q_INVOKABLE QString addStyleToLibrary(const QString &styPath);          // vuoto se è andata
    Q_INVOKABLE void removeLibraryStyle(const QString &name);

    // Le regole editoriali delle Preferenze, per tutti i progetti: id, title, text, custom.
    Q_INVOKABLE QVariantList ruleSections() const;
    Q_INVOKABLE void setRuleText(const QString &id, const QString &text);
    Q_INVOKABLE void resetRule(const QString &id);

    // La cronologia del progetto (⌘Z, ⇧⌘Z). I titoli sono vuoti se non c'è niente da annullare o da ripetere.
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void goToStep(int index);
    Q_INVOKABLE QString undoTitle() const;
    Q_INVOKABLE QString redoTitle() const;
    Q_INVOKABLE QVariantList historySteps() const;                         // title, when, current
    // Un passo della cronologia con ciò che è cambiato finora (prima di un lavoro: le modifiche a mano; dopo: il lavoro).
    Q_INVOKABLE void checkpoint(const QString &title);

    // La resa compilata di un pezzo, per i pannelli Rivedi (kind: prosa | latex; color: il colore del testo, «rrggbb»).
    // L'ora dell'ultima modifica di un file (in millisecondi), per l'indirizzo di un'immagine che si rifà con lo stesso nome.
    Q_INVOKABLE QString fileStamp(const QString &path) const;
    Q_INVOKABLE void say(const QString &text);         // un messaggio breve nella barra di stato

signals:
    void projectChanged();
    void recentsChanged();
    void treeChanged();
    void compilingChanged();
    void progressChanged();
    void issuesChanged();
    void pdfChanged();
    void jobChanged();
    void jobProgressChanged();
    void jobLogChanged();
    void busyChanged();
    void usageChanged();
    void waitingLimitChanged();
    void outcomeReady(const QVariantMap &outcome);
    void planReady(const QString &id);
    void planEdited(const QString &id);                // una modifica dell'utente al piano, già salvata
    void consoleChanged();
    void tagsChanged();
    void styleProposed(const QString &name, const QVariantList &environments);
    void styleQuestions(const QString &name, const QVariantList &questions);
    void outlineReady(const QVariantList &topics);
    void fileRenamed(const QString &from, const QString &to);
    void agentAsked(const QString &message, const QString &title);
    void proposalsChanged();   // una procedura da affidare all'agente, in una chat sua con il suo nome
    void runChanged();
    void runOpened(const QString &id);                 // aprire la finestra del lavoro
    void settingsChanged();
    void styleChanged();
    void filesChanged();                               // qualcosa nel progetto è cambiato su disco
    void pdfRebuilt();                                 // il PDF mostrato è stato riscritto
    void message(const QString &text);
    void buildFinished();
    void prepareAsked(const QString &path);            // una cartella che non è un progetto: chiedere se prepararla
    void settingsAsked();                              // aprire le impostazioni del progetto (da un piano: «Cambia»)
    void gesture(const QString &name, const QString &argument);   // le prove senza mani

private:
    Workspace();
    void open(const QString &path);
    void refreshTree();
    void watch();
    void onBuildFinished(const tx::BuildResult &result);
    void remember(const QString &path);
    void moved(const tx::History::Outcome &outcome);
    // forRun: il lavoro di «Avvia», i suoi eventi vanno nella sua finestra
    void startJob(const QString &title, std::function<std::function<void()>(tx::JobControl *)> work, bool forRun = false);
    // un lavoro che il limite ha fermato, di nuovo: se il limite c'è ancora aspetta, altrimenti riparte (le cache dei lavori
    // fanno sì che ciò che era fatto non si rifaccia)
    void restartJob(const QString &title, std::function<std::function<void()>(tx::JobControl *)> work, bool forRun);
    void paginateLessons(const QStringList &lessons, const QString &title);
    void convertFinished(const tx::Convert::Result &result);
    void decided(const QString &lesson, const QString &title, const QString &error);
    void sortJob(const QStringList &sources, const QJsonObject &previous, const QString &replaces);
    void nextSort();                                   // il riconoscimento che aspettava la fine di un lavoro
    void nextLater();                                  // ciò che aspettava il limite, quando si è azzerato e niente lavora
    void saveUnrecognized();                           // ciò che non si è riconosciuto, e perché: build/lavori/smistamento/
    // il lavoro di «Avvia»: gli eventi dal thread del lavoro, la fine, ciò che la finestra mostra (e build/…/vista.json)
    void startRun(const QString &id);
    void runEvent(const QJsonObject &event);
    void runFinished(const tx::run::Result &result);
    void loadRun();
    void stillRun();                                   // il lavoro non gira: ciò che era a metà torna da fare
    void saveRunView() const;
    QString m_runId, m_runState;
    QDateTime m_runUntil;
    bool m_runConnection = false;                      // il lavoro di «Avvia» si è fermato perché mancava la connessione
    qint64 m_runStarted = 0, m_runWorked = 0;          // quando è ripartito (0: fermo); il tempo lavorato prima
    double m_runEstimate = 0;
    QList<QVariantMap> m_runSites, m_runCalls;
    QHash<QString, int> m_runSiteAt;                   // chiave del cantiere → posto in m_runSites
    QHash<int, int> m_runCallAt;                       // id della chiamata → posto in m_runCalls
    QHash<QString, QString> m_runLessonSite;           // «src7» → la chiave del suo cantiere (per le chiamate senza cantiere)
    QHash<int, QVariantList> m_runStreams;             // il flusso delle chiamate dell'agente in corso
    QList<QVariantMap> m_runSteps;                     // i passi che non sono chiamate (compilazioni, prove), gli ultimi STEPS_KEPT
    QHash<int, int> m_runStepAt;                       // id del passo → posto in m_runSteps
    int lastRunId() const;                             // il numero più alto fra chiamate e passi: chi riprende continua da lì
    QVariantMap m_runOutcome;
    QTimer m_runTimer;                                 // più eventi vicini, un aggiornamento solo
    bool m_closing = false;
    QDateTime m_pausedUntil;                           // il lavoro in corso aspetta la fine del limite di utilizzo
    bool m_pauseConnection = false, m_offline = false;   // la pausa è per la connessione; le chiamate aspettano la rete
    void loadSettings();                               // impostazioni e stile, dal disco

    tx::Project m_project;
    tx::Compiler m_compiler;
    tx::Settings m_settings;
    tx::Style m_style = tx::Style::classic();
    QVariantList m_proposals;                          // le proposte dell'agente su profilo e stile, da decidere
    void readProposals();
    std::unique_ptr<tx::History> m_history;
    std::unique_ptr<tx::JobControl> m_jobControl;
    QThread *m_jobThread = nullptr;
    QString m_jobTitle, m_jobLabel;
    double m_jobProgress = 0;
    bool m_agentRunning = false;
    QStringList m_jobLog;
    QStringList m_console, m_hiddenMarks;
    QHash<QString, QString> m_unrecognized;            // in arrivo, non riconosciuto → perché
    QList<QStringList> m_sortWaiting;                  // da riconoscere appena niente lavora (la lezione scelta: newlesson::chosenLessons)
    QStringList m_converting;                          // le fonti della conversione in corso
    QVariantList m_tags;
    QString m_outcomeHead;                             // una riga in testa al prossimo esito di Converti
    bool m_showTags = false;
    int m_tagsRun = 0;                                 // l'ultimo calcolo dei tag chiesto: quelli di prima si buttano
    void computeTags();
    QFileSystemWatcher m_watcher;
    QTimer m_refresh;                                  // più cambiamenti vicini, un aggiornamento solo
    QTimer m_usageTick;                                // la barra dell'utilizzo: «di N minuti fa», il ritmo
    QDateTime m_workStarted;                           // da quando si lavora (un lavoro, l'agente): il ritmo dell'utilizzo
    // ciò che aspetta il limite di Claude: riparte da sé quando la finestra si azzera
    struct Later {
        QString title;
        std::function<void()> again;
        QDateTime until;
        QString key;
    };
    QList<Later> m_later;
    bool m_tryAnyway = false;                          // «Adesso»: si prova una volta senza guardare il limite ricordato
    QVariantList m_recent, m_outcomes, m_strays, m_jobs, m_lessons, m_documentFiles, m_queue, m_documents, m_images, m_issues;
    QHash<QString, QString> m_states;                  // lezione → stato dell'ultima compilazione
    QHash<QString, QString> m_details;
    double m_progress = 0;
    QString m_progressLabel, m_pdf;
    QHash<QString, QByteArray> m_shotsTried;            // il testo di una lezione di cui si sono già rifatte le pagine in piccolo
    QSet<QString> m_figuresTried;                       // le figure (nome e codice) di cui si è già provata la resa
    int m_errorCount = 0, m_openDoubts = 0, m_openDeclared = 0, m_openFigures = 0, m_openLayout = 0, m_changeCount = 0;
};
