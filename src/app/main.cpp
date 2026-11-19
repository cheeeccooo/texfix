// Texfix: una finestra sola in Qt Quick sul motore (src/core), chiamato direttamente.
//   Texfix [cartella]                                apre il progetto (senza: la pagina di benvenuto)
//   Texfix --strumenti-progetto <cartella>          gli strumenti dell'agente di un lavoro (li lancia il CLI `claude`)
//   Texfix [cartella] --foto f.png [--gesti "…"] [--dimensione LxA]
//                                                    prova senza mani: esegue i gesti, fotografa la finestra ed esce
#include "Chat.h"
#include "PdfBackend.h"
#include "Theme.h"
#include "Workspace.h"
#include "core/AgentTools.h"
#include "core/Tools.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QQmlApplicationEngine>
#include <QQmlEngineExtensionPlugin>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

Q_IMPORT_QML_PLUGIN(TexfixStylePlugin)    // lo stile dei controlli (qml/style)

namespace {

QString serverName()
{
#if defined(Q_OS_UNIX)
    return QStringLiteral("texfix-nuovo-%1").arg(::getuid());
#else
    return QStringLiteral("texfix-nuovo-%1").arg(qEnvironmentVariable("USERNAME"));
#endif
}

// Un'istanza sola: se Texfix è già aperto gli passa la cartella e torna true.
bool handedToRunningApp(const QString &folder)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(300))
        return false;
    socket.write(folder.toUtf8());
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

void listenForRequests(QQuickWindow *window)
{
    QLocalServer::removeServer(serverName());           // quello rimasto da una chiusura sbagliata
    auto *server = new QLocalServer(window);
    QObject::connect(server, &QLocalServer::newConnection, window, [server, window] {
        QLocalSocket *socket = server->nextPendingConnection();
        QObject::connect(socket, &QLocalSocket::disconnected, window, [socket, window] {
            const QString folder = QString::fromUtf8(socket->readAll());
            socket->deleteLater();
            if (!folder.isEmpty())
                Workspace::instance()->openFolder(folder);
            window->raise();
            window->requestActivate();
        });
    });
    server->listen(serverName());
}

// La prova senza mani: un gesto alla volta, ognuno quando il programma è fermo; poi la foto.
void runHandsFree(QQuickWindow *window, QStringList gestures, const QString &photo)
{
    auto *timer = new QTimer(window);
    timer->setInterval(350);
    qint64 waitUntil = 0;
    QObject::connect(timer, &QTimer::timeout, window, [=]() mutable {
        // «aspetta N» e «foto-ora FILE» non aspettano che il programma sia fermo: per vedere un lavoro mentre gira
        if (QDateTime::currentMSecsSinceEpoch() < waitUntil)
            return;
        const QString next = gestures.isEmpty() ? QString() : gestures.first().trimmed();
        if (next.startsWith("aspetta ")) {
            gestures.takeFirst();
            waitUntil = QDateTime::currentMSecsSinceEpoch() + qint64(next.section(' ', 1, 1).toDouble() * 1000);
            return;
        }
        if (next.startsWith("foto-ora ")) {
            gestures.takeFirst();
            window->grabWindow().save(next.section(' ', 1));
            return;
        }
        if ((Workspace::instance()->busy() && !Chat::instance()->waiting()) || Workspace::instance()->rendering())
            return;
        if (!gestures.isEmpty()) {
            const QString gesture = gestures.takeFirst().trimmed();
            if (gesture.startsWith("clic ")) {         // un clic vero del mouse, nel punto X Y della finestra (in punti)
                const QPointF at(gesture.section(' ', 1, 1).toDouble(), gesture.section(' ', 2, 2).toDouble());
                QMouseEvent press(QEvent::MouseButtonPress, at, window->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, at, window->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &release);
                return;
            }
            if (gesture.startsWith("passa ") || gesture.startsWith("destro ")) {   // il puntatore fermo su X Y; «destro»: un clic col tasto destro
                const QPointF at(gesture.section(' ', 1, 1).toDouble(), gesture.section(' ', 2, 2).toDouble());
                QMouseEvent move(QEvent::MouseMove, at, window->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &move);
                if (gesture.startsWith("destro ")) {
                    QMouseEvent press(QEvent::MouseButtonPress, at, window->mapToGlobal(at), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &press);
                    QMouseEvent release(QEvent::MouseButtonRelease, at, window->mapToGlobal(at), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &release);
                }
                return;
            }
            if (gesture.startsWith("rotella ")) {      // la rotella del mouse nel punto X Y: PASSI in giù (negativi: in su)
                const QPointF at(gesture.section(' ', 1, 1).toDouble(), gesture.section(' ', 2, 2).toDouble());
                const int steps = gesture.section(' ', 3, 3).toInt();
                QWheelEvent wheel(at, window->mapToGlobal(at), QPoint(0, -steps * 40), QPoint(0, -steps * 120), Qt::NoButton, Qt::NoModifier,
                                  Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &wheel);
                return;
            }
            emit Workspace::instance()->gesture(gesture.section(' ', 0, 0), gesture.section(' ', 1));
            return;
        }
        timer->stop();
        QTimer::singleShot(900, window, [=] {           // il tempo di disegnare le pagine del PDF
            const bool saved = window->grabWindow().save(photo);
            QCoreApplication::exit(saved ? 0 : 1);
        });
    });
    timer->start();
}

}

int main(int argc, char *argv[])
{
    // il passacarte degli strumenti dell'agente: senza finestra
    for (int i = 1; i + 1 < argc; ++i) {
        if (QByteArray(argv[i]) == "--strumenti") {
            QCoreApplication relay(argc, argv);
            return Chat::relay(QString::fromUtf8(argv[i + 1]));
        }
        if (QByteArray(argv[i]) == "--strumenti-progetto") {    // gli strumenti dell'agente di un lavoro, sul progetto su disco
            QCoreApplication tools(argc, argv);
            return tx::agent::serve(QString::fromUtf8(argv[i + 1]));
        }
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Texfix");
    QCoreApplication::setOrganizationDomain("texfix.checco.it");
    QCoreApplication::setApplicationName("Texfix (nuovo)");
    tx::tools::adoptPath();

    QCommandLineParser parser;
    parser.addPositionalArgument("cartella", "Il progetto da aprire.");
    parser.addOptions({{"foto", "Fotografa la finestra ed esce.", "file"},
                       {"gesti", "Gesti da eseguire prima della foto, separati da «;».", "gesti"},
                       {"dimensione", "La misura della finestra, LxA.", "LxA"}});
    parser.addHelpOption();
    parser.process(app);
    const QString folder = parser.positionalArguments().isEmpty() ? QString() : QDir(parser.positionalArguments().first()).absolutePath();
    const QString photo = parser.value("foto");
    if (photo.isEmpty() && handedToRunningApp(folder))
        return 0;

    QQuickStyle::setStyle("TexfixStyle");          // menu e suggerimenti di Texfix; il resto è Basic
    QQmlApplicationEngine engine;
    engine.addImageProvider("pdf", new PdfPages);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Texfix", "Main");
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    if (const QStringList size = parser.value("dimensione").split('x'); size.size() == 2)
        window->resize(size[0].toInt(), size[1].toInt());

    if (!folder.isEmpty()) {
        const QString answer = Workspace::instance()->openFolder(folder);
        if (answer == "prepara")
            emit Workspace::instance()->prepareAsked(folder);
        else if (!answer.isEmpty())
            Workspace::instance()->say(answer);
    }
    if (photo.isEmpty())
        listenForRequests(window);
    else
        runHandsFree(window, parser.value("gesti").split(';', Qt::SkipEmptyParts), photo);
    return app.exec();
}
