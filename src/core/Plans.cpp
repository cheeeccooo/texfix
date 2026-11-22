#include "core/Plans.h"

#include "core/Files.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace tx::plans {

namespace {
QString dir(const Project &project, const QString &id) { return project.path("salvataggi/lavori/" + id); }
}

QList<Info> open(const Project &project)
{
    QList<Info> out;
    for (const QString &id : QDir(project.path("salvataggi/lavori")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QJsonObject plan = load(project, id);
        if (!plan.isEmpty())
            out << Info{id, plan.value("titolo").toString(), plan.value("stato").toString()};
    }
    return out;
}

QJsonObject load(const Project &project, const QString &id)
{
    return QJsonDocument::fromJson(files::read(dir(project, id) + "/piano.json").toUtf8()).object();
}

bool save(const Project &project, const QString &id, const QJsonObject &plan)
{
    return files::write(dir(project, id) + "/piano.json", QString::fromUtf8(QJsonDocument(plan).toJson()));
}

QString create(const Project &project, const QJsonObject &delivered, QString *error)
{
    QJsonArray sections;
    int items = 0;
    for (const QJsonValue &s : delivered.value("sezioni").toArray()) {
        QJsonArray entries;
        for (const QJsonValue &v : s.toObject().value("voci").toArray()) {
            const QString text = v.isString() ? v.toString() : v.toObject().value("testo").toString();
            if (!text.trimmed().isEmpty())
                entries.append(QJsonObject{{"testo", text.trimmed()}, {"fare", true}, {"nota", ""}});
        }
        items += entries.size();
        if (!entries.isEmpty())
            sections.append(QJsonObject{{"titolo", s.toObject().value("titolo").toString()}, {"voci", entries}});
    }
    if (delivered.value("titolo").toString().trimmed().isEmpty() || items == 0) {
        if (error)
            *error = QStringLiteral("Un piano ha un titolo e almeno una sezione con una voce.");
        return {};
    }
    QJsonArray questions;
    for (const QJsonValue &q : delivered.value("domande").toArray())
        if (!q.toObject().value("testo").toString().isEmpty())
            questions.append(QJsonObject{{"testo", q.toObject().value("testo")}, {"opzioni", q.toObject().value("opzioni").toArray()}, {"risposta", ""}});
    QString id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    while (QFileInfo::exists(dir(project, id)))
        id += 'b';
    save(project, id, QJsonObject{{"titolo", delivered.value("titolo").toString().trimmed()}, {"riepilogo", delivered.value("riepilogo").toString()},
                                  {"stato", "piano"}, {"sezioni", sections}, {"domande", questions}, {"commento", ""}});
    return id;
}

QString setOutcome(const Project &project, const QString &id, const QString &text)
{
    QJsonObject plan = load(project, id);
    if (plan.isEmpty())
        return QStringLiteral("Il lavoro «%1» non c'è.").arg(id);
    plan["esito"] = text;
    plan["stato"] = "esito";
    save(project, id, plan);
    return {};
}

void discard(const Project &project, const QString &id)
{
    if (!id.isEmpty())
        QDir(dir(project, id)).removeRecursively();
}

bool hasInstructions(const QJsonObject &plan)
{
    if (!plan.value("commento").toString().trimmed().isEmpty() || !plan.value("nota_scaletta").toString().trimmed().isEmpty())
        return true;
    for (const QJsonValue &s : plan.value("sezioni").toArray())
        for (const QJsonValue &v : s.toObject().value("voci").toArray())
            if (!v.toObject().value("nota").toString().trimmed().isEmpty())
                return true;
    // una risposta proposta che l'utente ha cambiato, o una scelta di sezione che chiede di rifare il piano
    for (const QJsonValue &q : plan.value("domande").toArray())
        if (q.toObject().contains("proposta") && q.toObject().value("proposta") != q.toObject().value("risposta"))
            return true;
    for (const QJsonValue &s : plan.value("sezioni").toArray()) {
        const QJsonObject choice = s.toObject().value("scelta").toObject();
        if (!choice.isEmpty() && choice.value("rifai").toArray().contains(choice.value("valore")))
            return true;
    }
    // il piano dei concetti: un'istruzione sotto una destinazione, una versione da smistare come materiale nuovo
    for (const QJsonValue &d : plan.value("destinazioni").toArray())
        if (!d.toObject().value("nota").toString().trimmed().isEmpty())
            return true;
    for (const QJsonValue &v : plan.value("versioni").toArray())
        if (v.toObject().value("valore").toString() == "nuovo")
            return true;
    return false;
}

QString markdown(const QJsonObject &plan)
{
    QStringList lines{"## " + plan.value("titolo").toString(), QString()};
    if (!plan.value("riepilogo").toString().isEmpty())
        lines << plan.value("riepilogo").toString() << QString();
    for (const QJsonValue &s : plan.value("sezioni").toArray()) {
        lines << "### " + s.toObject().value("titolo").toString() << QString();
        for (const QJsonValue &v : s.toObject().value("voci").toArray()) {
            const QJsonObject entry = v.toObject();
            lines << QStringLiteral("- [%1] %2").arg(entry.value("fare").toBool() ? "x" : " ", entry.value("testo").toString());
            if (!entry.value("nota").toString().trimmed().isEmpty())
                lines << "  ↳ " + entry.value("nota").toString().trimmed();
        }
        lines << QString();
    }
    for (const QJsonValue &q : plan.value("domande").toArray())
        lines << QStringLiteral("- %1 **%2**").arg(q.toObject().value("testo").toString(),
                                                    q.toObject().value("risposta").toString().isEmpty() ? QStringLiteral("(senza risposta)") : q.toObject().value("risposta").toString());
    if (!plan.value("commento").toString().trimmed().isEmpty())
        lines << QString() << "Commento: " + plan.value("commento").toString().trimmed();
    return lines.join('\n').trimmed() + '\n';
}

QString startMessage(const QString &id, const QJsonObject &plan)
{
    return QStringLiteral("Il piano del lavoro «%1» è approvato. Eseguilo così com'è qui sotto: le voci con [x] si fanno, quelle con [ ] no; "
                          "le risposte alle domande valgono. Alla fine consegna l'esito con consegna_esito (lavoro: %1).\n\n%2").arg(id, markdown(plan));
}

QString reviseMessage(const QString &id, const QJsonObject &plan)
{
    return QStringLiteral("Rivedi il piano del lavoro «%1» seguendo le mie istruzioni (le righe con ↳ sotto le voci, e il commento), poi "
                          "consegnalo di nuovo con consegna_piano indicando lavoro: %1. Non eseguire ancora niente.\n\n%2").arg(id, markdown(plan));
}

QString archive(const Project &project, const QString &id)
{
    const QJsonObject plan = load(project, id);
    if (plan.isEmpty())
        return {};
    QString title = plan.value("titolo").toString();
    title.replace('/', '-').remove(':');
    const QString file = files::freeName(project.path(QStringLiteral("salvataggi/esiti/%1 — %2.md").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm"), title)));
    files::write(file, QStringLiteral("# %1\n\n%2\n\n---\n\nIl piano seguito:\n\n%3").arg(plan.value("titolo").toString(), plan.value("esito").toString().trimmed(), markdown(plan)));
    discard(project, id);
    return file;
}

}
