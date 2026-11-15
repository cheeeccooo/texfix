#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QObject>
#include <QProcess>
#include <QQmlEngine>
#include <QVariantList>

// La chat con l'agente: Claude Code locale (il CLI `claude`), chiuso nel progetto. Gli strumenti di Texfix glieli dà
// il programma: il CLI lancia `Texfix --strumenti <nome>`, un passacarte che porta le sue richieste a questo processo
// su un canale locale (nessuna porta di rete); le esegue il motore, sul progetto aperto.
class Chat : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // I messaggi: role (user | assistant | tool | note), text.
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)   // sonnet | opus
    // Quanto l'agente fa da solo, come in Claude Code: auto (decide lui che cosa è sicuro, chiede per il resto),
    // chiedi (chiede conferma prima di ogni modifica), autonomo (non chiede mai). Vale per tutti i progetti.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool waiting READ waiting NOTIFY waitingChanged)           // aspetta una conferma dell'utente

public:
    static Chat *instance();
    static Chat *create(QQmlEngine *, QJSEngine *);
    // Il passacarte: `Texfix --strumenti <nome>` porta righe da stdin al programma aperto e le risposte a stdout.
    static int relay(const QString &serverName);

    QVariantList messages() const { return m_messages; }
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    bool waiting() const { return m_asking != nullptr; }
    Q_INVOKABLE void answer(bool allow);               // la risposta a una richiesta di conferma

    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();

signals:
    void messagesChanged();
    void runningChanged();
    void modelChanged();
    void modeChanged();
    void waitingChanged();

private:
    Chat();
    void readOutput();
    void finished();
    void add(const QString &role, const QString &text);
    void appendToAssistant(const QString &text);
    void save() const;
    void load();

    QProcess m_process;
    QLocalServer m_tools;
    QByteArray m_buffer;
    QVariantList m_messages;
    QString m_model = "sonnet", m_session, m_id;
    QString m_mode;
    QPointer<QLocalSocket> m_asking;                   // chi aspetta la conferma, la sua richiesta e che cosa voleva fare
    QJsonValue m_askingId;
    QJsonObject m_askingInput;
    bool m_streamed = false;                           // il testo di questo messaggio è già arrivato a pezzi
};
