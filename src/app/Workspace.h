#pragma once
#include "core/Compiler.h"
#include "core/Convert.h"
#include "core/History.h"
#include "core/Settings.h"
#include "core/Style.h"
#include "core/Project.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QThread>
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
    // Le lezioni del documento: file, number, date, title, state (idle | good | warn | bad), detail.
    Q_PROPERTY(QVariantList lessons READ lessons NOTIFY treeChanged)
    // Gli altri file del documento: main.tex, texfix.sty e i .tex di src/ che il main non richiama.
    Q_PROPERTY(QVariantList documentFiles READ documentFiles NOTIFY treeChanged)
    // Le fonti da convertire (fonti/coda), i documenti da consultare (docs/), le immagini (imgs/): file, name.
    Q_PROPERTY(QVariantList queue READ queue NOTIFY treeChanged)
    Q_PROPERTY(QVariantList documents READ documents NOTIFY treeChanged)
    Q_PROPERTY(QVariantList images READ images NOTIFY treeChanged)
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
    // Il lavoro in corso (Converti…): uno alla volta. busy: un lavoro o una compilazione.
    Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobChanged)
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY jobProgressChanged)
    Q_PROPERTY(QString jobLabel READ jobLabel NOTIFY jobProgressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool resumable READ resumable NOTIFY jobChanged)           // un lavoro interrotto, da riprendere
    Q_PROPERTY(QStringList jobLog READ jobLog NOTIFY jobLogChanged)       // il registro del lavoro (Console)
    // Le regole del progetto, una per voce (Impostazioni del progetto).
    Q_PROPERTY(QStringList projectRules READ projectRules WRITE setProjectRules NOTIFY settingsChanged)
    Q_PROPERTY(QString styleName READ styleName NOTIFY styleChanged)

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
    bool jobRunning() const { return m_jobThread != nullptr; }
    QString jobTitle() const { return m_jobTitle; }
    double jobProgress() const { return m_jobProgress; }
    QString jobLabel() const { return m_jobLabel; }
    bool busy() const { return compiling() || jobRunning() || m_agentRunning; }
    bool resumable() const { return hasProject() && !jobRunning() && !tx::Convert::interrupted(m_project).isEmpty(); }
    QStringList jobLog() const { return m_jobLog; }
    QStringList projectRules() const { return m_settings.rules; }
    void setProjectRules(const QStringList &rules);
    QString styleName() const { return m_style.name; }
    const tx::Style &style() const { return m_style; }
    const tx::Project &project() const { return m_project; }
    // L'agente sta lavorando: come un lavoro, niente altro parte e niente si annulla.
    void setAgentRunning(bool running);
    void agentTurnDone();
    const tx::Settings &settings_() const { return m_settings; }

    // Apre una cartella. «» se è aperta; «prepara» se non è ancora un progetto (serve il sì dell'utente, poi
    // prepareFolder); altrimenti il perché non si apre.
    Q_INVOKABLE QString openFolder(const QString &path);
    Q_INVOKABLE void prepareFolder(const QString &path);
    // Un progetto nuovo, anche da un profilo di partenza (vuoto: nessuno). I profili: i nomi; saveProfile salva le scelte
    // del progetto aperto (vuoto se è andata).
    Q_INVOKABLE QString newProject(const QString &path, const QString &profile = {});
    Q_INVOKABLE QStringList profiles() const;
    Q_INVOKABLE QString saveProfile(const QString &name);
    Q_INVOKABLE void removeProfile(const QString &name);
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
    Q_INVOKABLE void keepDeclared(const QString &lesson, const QString &hash, bool keep);
    Q_INVOKABLE void keepAllDeclared(const QString &lesson);
    Q_INVOKABLE void dropDeclared(const QString &lesson, const QString &hash);
    Q_INVOKABLE void restoreDeclared(const QString &lesson, const QString &hash);

    // Rivedi figure: tutte le figure del progetto, lezione per lezione (type lesson | figure). redoFigure la ridisegna
    // (how: ridisegna) o la ritaglia dalla fonte (ritaglia): un lavoro, un passo della cronologia.
    Q_INVOKABLE QVariantList figureItems() const;
    Q_INVOKABLE void redoFigure(const QString &id, const QString &how);

    // «Risolvi tutto»: errori e righe fuori misura delle lezioni non fatte (prima le mosse che non costano, poi il
    // modello), poi compila il documento. Un lavoro, un passo della cronologia.
    Q_INVOKABLE void fixAll();

    // Impaginazione. paginate impagina le lezioni non fatte che sono nuove o cambiate (un lavoro, un passo della
    // cronologia). layoutItems: le mosse, lezione per lezione (type lesson | move). decideMove: accettata | lasciata.
    Q_INVOKABLE void paginate();
    Q_INVOKABLE QVariantList layoutItems() const;
    Q_INVOKABLE void decideMove(const QString &lesson, const QString &block, const QString &state);

    // I lavori con un piano. jobs: quelli aperti (id, title, state). plan: il piano com'è; savePlan lo salva subito.
    // planMessage: ciò che si dice all'agente per avviarlo (how: avvia) o rifarlo (rivedi). planReady: l'agente ha
    // consegnato un piano o un esito.
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY treeChanged)
public:
    QVariantList jobs() const { return m_jobs; }
    Q_INVOKABLE QVariantMap plan(const QString &id) const;
    Q_INVOKABLE void savePlan(const QString &id, const QVariantMap &plan);
    Q_INVOKABLE bool planHasInstructions(const QString &id) const;
    Q_INVOKABLE QString planMessage(const QString &id, const QString &how);
    Q_INVOKABLE void discardPlan(const QString &id);
    Q_INVOKABLE void archivePlan(const QString &id);

    // Nuova lezione e Aggiungi materiale. addSources porta dei file fra le fonti da convertire. lessonMarks: numero e data
    // proposti (number, date) o letti dalle fonti, con conflict se non concordano. createLesson: una fonte si converte
    // con il numero e la data scelti; più fonti le fonde l'agente, con un piano (agentAsked). integrateSources: l'agente
    // porta in una lezione ciò che manca, con un piano.
    Q_INVOKABLE QStringList addSources(const QStringList &paths);
    Q_INVOKABLE QVariantMap lessonMarks(const QStringList &paths) const;
    Q_INVOKABLE QString createLesson(int number, const QString &date, const QStringList &paths);
    Q_INVOKABLE void integrateSources(const QString &lesson, const QStringList &paths);
    Q_INVOKABLE static QStringList localPaths(const QList<QUrl> &urls);

    // Lo smistamento del materiale (Aggiungi materiale): un piano che fa Texfix, tipo «materiale». sortMaterial lo
    // prepara (un lavoro); startSortPlan divide i pezzi e fa partire le lezioni: quelle semplici si convertono, le altre
    // passano dall'agente una alla volta; reviseSortPlan lo rifà con le istruzioni dell'utente.
    Q_INVOKABLE void sortMaterial(const QStringList &sources);
    Q_INVOKABLE void startSortPlan(const QString &id);
    Q_INVOKABLE void reviseSortPlan(const QString &id);

    // Version control: lo usa solo l'utente. changeCount: quante modifiche non salvate (nel titolo della linguetta).
    Q_PROPERTY(int changeCount READ changeCount NOTIFY treeChanged)
    Q_PROPERTY(bool hasRepository READ hasRepository NOTIFY treeChanged)
public:
    int changeCount() const { return m_changeCount; }
    bool hasRepository() const;
    Q_INVOKABLE QVariantList gitChanges() const;                           // file, state
    Q_INVOKABLE QVariantList gitLog() const;                               // hash, when, subject
    Q_INVOKABLE QString gitDiff(const QString &what) const;                // «diff:<file>» o «commit:<hash>»
    Q_INVOKABLE QString gitCommit(const QString &message);                 // vuoto se è andata
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
    Q_PROPERTY(bool byTopic READ byTopic NOTIFY settingsChanged)
public:
    bool byTopic() const { return m_settings.byTopic(); }
    Q_INVOKABLE void proposeTopics();
    Q_INVOKABLE QString applyTopics(const QVariantList &topics);          // vuoto se è andata
    Q_INVOKABLE void backToLessons();

    // Converti. Le fonti di «Da convertire»: file, name, detail, convertible; la stima del costo in dollari.
    // convert le fa diventare lezioni (un passo solo della cronologia), poi compila il documento; alla fine
    // outcomeReady con title, lines, cost, minutes. resume riprende un lavoro interrotto senza rifare ciò che è fatto.
    Q_INVOKABLE QVariantList convertSources() const;
    Q_INVOKABLE double convertEstimate() const;
    Q_INVOKABLE void convert(const QStringList &sources);
    void convert(const QStringList &sources, const QHash<QString, tx::Convert::Wanted> &wanted);
    Q_INVOKABLE void resume();

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
    Q_INVOKABLE QVariantList styleRoles() const;                           // value, label
    Q_INVOKABLE void setStyleRoles(const QString &name, const QVariantMap &roles);   // ambiente → ruolo
    Q_INVOKABLE void applyStyle(const QString &name);

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
    void outcomeReady(const QVariantMap &outcome);
    void planReady(const QString &id);
    void consoleChanged();
    void outlineReady(const QVariantList &topics);
    void fileRenamed(const QString &from, const QString &to);
    void agentAsked(const QString &message);          // una procedura da affidare all'agente, nella chat
    void settingsChanged();
    void styleChanged();
    void filesChanged();                               // qualcosa nel progetto è cambiato su disco
    void pdfRebuilt();                                 // il PDF mostrato è stato riscritto
    void message(const QString &text);
    void buildFinished();
    void prepareAsked(const QString &path);            // una cartella che non è un progetto: chiedere se prepararla
    void gesture(const QString &name, const QString &argument);   // le prove senza mani

private:
    Workspace();
    void open(const QString &path);
    void refreshTree();
    void watch();
    void onBuildFinished(const tx::BuildResult &result);
    void remember(const QString &path);
    void moved(const tx::History::Outcome &outcome);
    void startJob(const QString &title, std::function<std::function<void()>(tx::JobControl *)> work);
    void convertFinished(const tx::Convert::Result &result);
    void decided(const QString &lesson, const QString &title, const QString &error);
    void sortJob(const QStringList &sources, const QString &previous, const QString &replaces);
    void nextProcedure();                              // la prossima procedura dell'agente in coda, se niente lavora
    void loadSettings();                               // impostazioni e stile, dal disco

    tx::Project m_project;
    tx::Compiler m_compiler;
    tx::Settings m_settings;
    tx::Style m_style = tx::Style::classic();
    std::unique_ptr<tx::History> m_history;
    std::unique_ptr<tx::JobControl> m_jobControl;
    QThread *m_jobThread = nullptr;
    QString m_jobTitle, m_jobLabel;
    double m_jobProgress = 0;
    bool m_agentRunning = false;
    QStringList m_jobLog;
    QStringList m_agentQueue, m_console;                          // procedure dell'agente che aspettano il loro turno
    QFileSystemWatcher m_watcher;
    QTimer m_refresh;                                  // più cambiamenti vicini, un aggiornamento solo
    QVariantList m_jobs, m_lessons, m_documentFiles, m_queue, m_documents, m_images, m_issues;
    QHash<QString, QString> m_states;                  // lezione → stato dell'ultima compilazione
    QHash<QString, QString> m_details;
    double m_progress = 0;
    QString m_progressLabel, m_pdf;
    int m_errorCount = 0, m_openDoubts = 0, m_openDeclared = 0, m_openFigures = 0, m_openLayout = 0, m_changeCount = 0;
};
