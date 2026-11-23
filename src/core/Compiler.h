#pragma once
#include "core/Project.h"
#include "core/TexLog.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QProcessEnvironment>
#include <QTimer>

class QProcess;

namespace tx {

struct BuildResult {
    QString lesson;                        // la lezione compilata da sola; vuoto = il documento
    QString pdf;                           // percorso assoluto del PDF
    QString failure;                       // la compilazione non è partita (non un errore di LaTeX)
    QList<Issue> issues;
    bool stopped = false;                  // fermata dall'utente
    qint64 ms = 0;

    bool hasErrors() const;
};

// La compilazione, con latexmk e il .latexmkrc del progetto: il documento in build/main.pdf (come `latexmk` dalla
// cartella) oppure una lezione da sola, in build/lezioni/, con un main ridotto e una passata. main.tex non si tocca.
class Compiler : public QObject {
    Q_OBJECT
public:
    explicit Compiler(QObject *parent = nullptr);
    ~Compiler() override;

    void setProject(const Project &project) { m_project = project; }
    bool isRunning() const;

    void compileDocument();
    void compileLesson(const QString &file);
    void stop();

    static QString pdfOf(const Project &project, const QString &lesson);
    // La stessa compilazione, aspettando la fine: per i lavori, in un thread di lavoro. Si ferma con il lavoro.
    static BuildResult compileSync(const Project &project, const QString &lesson, class JobControl *control = nullptr);
    // Il main con il solo \input della lezione, e il contatore di sezione che avrebbe nel documento.
    static QString reducedMain(const Project &project, const QString &lesson, QString *error = nullptr);

signals:
    void started(const QString &lesson);
    void progress(double fraction, const QString &label);
    void finished(const tx::BuildResult &result);

private:
    void start(const QString &lesson);
    void launch();
    static QStringList arguments(const QString &lesson);
    static QProcessEnvironment environment();
    void readOutput();
    void finish();
    void makeProcess();
    void abandon();
    void dropAux() const;
    QString base() const;                  // build/main o build/lezioni/<nome>, senza estensione

    Project m_project;
    QProcess *m_process = nullptr;
    QTimer m_watch;                        // la sorveglianza: latexmk c'è ancora?
    int m_goneTicks = 0;
    BuildResult m_result;
    QElapsedTimer m_timer;
    QDateTime m_logBefore;                 // l'ora del log prima di partire: se non cambia, il log non è suo
    QByteArray m_output;
    qsizetype m_scanned = 0;
    int m_pass = 0, m_pages = 0, m_lastPage = 0, m_expectedPages = 0;
    bool m_retried = false;                // già ricompilata una volta per un .aux rovinato
};

}

Q_DECLARE_METATYPE(tx::BuildResult)
