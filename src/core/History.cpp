#include "core/History.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace tx {

namespace {
const qint64 MAX_FILE = 200LL * 1024 * 1024;
const char *const HOME = "build/cronologia";
const char *const FIRST = "Com'era all'apertura";
const char *const BY_HAND = "Modifiche a mano";

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    thread_local QCryptographicHash hash(QCryptographicHash::Sha1);   // riusato: crearlo ogni volta costa millisecondi
    hash.reset();
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QStringList trackedFiles(const QString &root)
{
    QStringList out;
    const QDir dir(root);
    for (const QString &name : dir.entryList({"main.tex", ".latexmkrc", "*.sty", "*.stile.json"}, QDir::Files | QDir::Hidden))
        out << name;
    for (const char *sub : {"src", "imgs", "fonti", "salvataggi"}) {
        for (QDirIterator it(root + '/' + sub, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();) {
            const QString file = dir.relativeFilePath(it.next());
            if (it.fileName().startsWith('.') || it.fileInfo().size() > MAX_FILE)
                continue;
            if (file.startsWith("salvataggi/chat/") || file.startsWith("salvataggi/lavori/") || file.startsWith("salvataggi/esiti/"))
                continue;
            out << file;
        }
    }
    return out;
}

QString nowText() { return QDateTime::currentDateTime().toString("dd/MM HH:mm"); }
}

History::History(const QString &root) : m_root(root)
{
    QDir(m_root + '/' + HOME).removeRecursively();     // quella rimasta da una chiusura sbagliata
    append(scan(), QString::fromUtf8(FIRST));
}

History::~History() { QDir(m_root + '/' + HOME).removeRecursively(); }

QString History::objectPath(const QString &hash) const
{
    return QStringLiteral("%1/%2/%3/%4").arg(m_root, QString::fromUtf8(HOME), hash.left(2), hash);
}

History::State History::scan()
{
    State state;
    QHash<QString, Seen> seen;
    for (const QString &file : trackedFiles(m_root)) {
        const QFileInfo info(m_root + '/' + file);
        Seen entry = m_seen.value(file);
        const qint64 size = info.size(), modified = info.lastModified().toMSecsSinceEpoch();
        if (entry.hash.isEmpty() || entry.size != size || entry.modified != modified)
            entry = {size, modified, hashFile(info.filePath())};
        if (entry.hash.isEmpty())
            continue;
        state.insert(file, entry.hash);
        seen.insert(file, entry);
    }
    m_seen = seen;
    return state;
}

void History::store(const State &state) const
{
    for (auto it = state.begin(); it != state.end(); ++it) {
        const QString object = objectPath(it.value());
        if (QFileInfo::exists(object))
            continue;
        QDir().mkpath(QFileInfo(object).absolutePath());
        QFile::copy(m_root + '/' + it.key(), object + ".tmp");
        QFile::rename(object + ".tmp", object);
    }
}

// I file come nel passo: si scrive solo ciò che è diverso, si tolgono i file seguiti che lì non c'erano.
QStringList History::restore(const State &target, const State &now) const
{
    QStringList changed;
    for (auto it = now.begin(); it != now.end(); ++it) {
        if (!target.contains(it.key())) {
            QFile::remove(m_root + '/' + it.key());
            changed << it.key();
        }
    }
    for (auto it = target.begin(); it != target.end(); ++it) {
        if (now.value(it.key()) == it.value())
            continue;
        const QString file = m_root + '/' + it.key(), object = objectPath(it.value());
        if (!QFileInfo::exists(object))
            continue;                                  // un contenuto perso: il file resta com'è
        QDir().mkpath(QFileInfo(file).absolutePath());
        const QString temp = file + ".texfix-tmp";
        QFile::remove(temp);
        if (QFile::copy(object, temp)) {
            QFile::remove(file);
            QFile::rename(temp, file);
            changed << it.key();
        }
    }
    changed.sort();
    return changed;
}

bool History::append(const State &now, const QString &title)
{
    if (m_cursor >= 0 && m_steps[m_cursor].files == now)
        return false;
    store(now);
    m_steps.erase(m_steps.begin() + m_cursor + 1, m_steps.end());   // ciò che era «avanti» non c'è più
    m_steps << Entry{title, nowText(), now};
    m_cursor = int(m_steps.size()) - 1;
    forget();
    return true;
}

void History::forget()
{
    if (m_steps.size() <= KEEP)
        return;
    const int drop = int(m_steps.size()) - KEEP;
    m_steps.erase(m_steps.begin(), m_steps.begin() + drop);
    m_cursor = qMax(0, m_cursor - drop);
    QSet<QString> used;
    for (const Entry &step : std::as_const(m_steps))
        for (const QString &hash : step.files)
            used.insert(hash);
    for (QDirIterator it(m_root + '/' + HOME, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        it.next();
        if (!used.contains(it.fileName()))
            QFile::remove(it.filePath());
    }
}

History::Outcome History::jump(const State &now, int target, const QString &what, bool back)
{
    Outcome outcome;
    outcome.changed = restore(m_steps[target].files, now);
    m_cursor = target;
    outcome.ok = true;
    outcome.message = QStringLiteral("%1: %2.").arg(back ? "Annullato" : "Ripetuto", what);
    return outcome;
}

bool History::record(const QString &title) { return append(scan(), title); }

History::Outcome History::undo()
{
    const State now = scan();
    append(now, QString::fromUtf8(BY_HAND));           // se ci sono modifiche a mano, si annullano quelle
    if (m_cursor <= 0)
        return {false, QStringLiteral("Niente da annullare."), {}};
    return jump(now, m_cursor - 1, m_steps[m_cursor].title, true);
}

History::Outcome History::redo()
{
    const State now = scan();
    if (append(now, QString::fromUtf8(BY_HAND)))
        return {false, QStringLiteral("Hai cambiato dei file dopo l'annullamento: non c'è più niente da ripetere."), {}};
    if (m_cursor + 1 >= m_steps.size())
        return {false, QStringLiteral("Niente da ripetere."), {}};
    return jump(now, m_cursor + 1, m_steps[m_cursor + 1].title, false);
}

History::Outcome History::goTo(int index)
{
    if (index < 0 || index >= m_steps.size())
        return {false, QStringLiteral("Quel passo non c'è più."), {}};
    const State now = scan();
    const bool byHand = append(now, QString::fromUtf8(BY_HAND));
    if (byHand && index >= m_cursor)                   // i passi avanti sono appena spariti
        return {false, QStringLiteral("Hai cambiato dei file dopo l'annullamento: non c'è più niente da ripetere."), {}};
    if (index == m_cursor)
        return {false, QStringLiteral("Sei già lì."), {}};
    const bool back = index < m_cursor;
    const QString what = back && m_cursor - index > 1 ? QStringLiteral("torno a «%1»").arg(m_steps[index].title)
                                                      : m_steps[back ? m_cursor : index].title;
    return jump(now, index, what, back);
}

QList<History::Step> History::steps() const
{
    QList<Step> out;
    for (const Entry &step : m_steps)
        out << Step{step.title, step.when};
    return out;
}

QString History::undoTitle()
{
    if (m_steps[m_cursor].files != scan())
        return QString::fromUtf8(BY_HAND);
    return m_cursor > 0 ? m_steps[m_cursor].title : QString();
}

QString History::redoTitle()
{
    if (m_cursor + 1 >= m_steps.size() || m_steps[m_cursor].files != scan())
        return {};
    return m_steps[m_cursor + 1].title;
}

}
