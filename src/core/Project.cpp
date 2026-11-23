#include "core/Project.h"

#include "core/Files.h"
#include "core/Pages.h"
#include "core/Rules.h"
#include "core/Tools.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace tx {

namespace {

// Senza stile un progetto è un classico documento matematico: article e amsthm, nessun colore.
const char *const MAIN_TEMPLATE = R"(\documentclass[11pt,a4paper]{article}
\usepackage[T1]{fontenc}
\usepackage[italian]{babel}
\usepackage{texfix}

\title{@TITOLO@}
\author{}
\date{}

\begin{document}

\maketitle
\tableofcontents
\clearpage

\end{document}
)";

const char *const PACKAGE_HEAD = R"(% Ciò che Texfix aggiunge al documento, e lo stile del progetto. Lo scrive il programma, per intero: le modifiche
% fatte qui si perdono.
\NeedsTeXFormat{LaTeX2e}
\ProvidesPackage{texfix}[2026/11/14 Texfix]
)";


const char *const PACKAGE_TAIL = R"(
% Impaginazione: un paragrafo non lascia una riga sola in fondo o in cima alla pagina, né prima di una formula
\clubpenalty=10000 \widowpenalty=10000 \displaywidowpenalty=10000
% (dove sta il testo nella pagina, scritto nel log: serve a misurare i vuoti)
\AtBeginDocument{\typeout{TEXFIX pagina: alto \the\dimexpr 1in+\voffset+\topmargin+\headheight+\headsep\relax, testo \the\textheight}}

% Le righe fuori misura: ogni sforamento oltre mezzo punto è segnalato nel log
\hfuzz=0.5pt

% Aggiunte e correzioni dichiarate da chi ha scritto gli appunti: testo più chiaro, con una sigla davanti
\newcommand\texfix@mark[1]{{\fboxsep=1.5pt\fboxrule=0.4pt\fcolorbox{black!45}{white}{\tiny\bfseries\color{black!60}#1}}\hspace{0.45em}\ignorespaces}
\@ifundefined{addedtext}{\newenvironment{addedtext}{\par\begingroup\color{black!62}\noindent\texfix@mark{+}}{\par\endgroup}}{}
\@ifundefined{correction}{\newenvironment{correction}{\par\begingroup\color{black!62}\noindent\texfix@mark{CORREZIONE}}{\par\endgroup}}{}

% Il segnaposto di una figura ancora da inserire: \imgslot[larghezza]{nome}{altezza su larghezza}{descrizione}
\providecommand{\imgslot}[4][0.8\linewidth]{\fbox{\parbox[c][#3\dimexpr#1\relax][c]{\dimexpr#1-2\fboxsep-2\fboxrule\relax}{%
  \centering\small Figura da inserire: #4}}}

% \lezione{N}{data}: la lezione da cui viene ciò che segue
\providecommand{\lezione}[2]{\par\noindent\textbf{%
  \if\relax\detokenize{#1}\relax\else Lezione~#1\if\relax\detokenize{#2}\relax\else\ --\ \fi\fi#2}\par\nobreak}
)";

const char *const LATEXMKRC = R"(# Compilazione del documento: `latexmk` dalla cartella del progetto -> build/main.pdf (ausiliari in build/).
# Texfix compila allo stesso modo e legge questo file: qui si possono aggiungere regole (bibliografia, indici).
$pdf_mode = 1;
$out_dir = 'build';
$pdflatex = 'pdflatex -interaction=nonstopmode -file-line-error -synctex=1 %O %S';
@default_files = ('main.tex');
# righe del log intere: gli avvisi si leggono con il loro file e la loro riga
$ENV{'max_print_line'} = 100000;
$ENV{'error_line'} = 254;
$ENV{'half_error_line'} = 238;
)";

const char *const GITIGNORE = "build/\nsalvataggi/chat/\nsalvataggi/cronologia/\n.DS_Store\n";

// Il contenuto di un gruppo {…} che si apre in `open`, con le graffe dentro bilanciate.
QString braced(QStringView text, qsizetype open)
{
    int depth = 0;
    for (qsizetype i = open; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == '\\') {
            ++i;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}' && --depth == 0) {
            return text.mid(open + 1, i - open - 1).toString();
        }
    }
    return {};
}

// Il testo senza i commenti, riga per riga (le righe restano le stesse).
QString withoutComments(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (QStringView line : QStringView(text).split('\n')) {
        out += uncommented(line);
        out += '\n';
    }
    return out;
}

}

QStringView uncommented(QStringView line)
{
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line[i] == '\\')
            ++i;
        else if (line[i] == '%')
            return line.left(i);
    }
    return line;
}

Project::Project(const QString &root) : m_root(QDir::cleanPath(QDir(root).absolutePath())) {}

QString Project::name() const { return QFileInfo(m_root).fileName(); }

QString Project::relative(const QString &absolute) const { return QDir(m_root).relativeFilePath(absolute); }

bool Project::isProject(const QString &dir) { return QFileInfo::exists(dir + '/' + STATE); }

QList<std::pair<QString, QString>> Project::loose(const QString &dir)
{
    QList<std::pair<QString, QString>> out;
    if (!QFileInfo(dir).isDir())
        return out;
    QStringList found;
    for (QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); it.hasNext();) {
        const QString rel = QDir(dir).relativeFilePath(it.next());
        const QStringList parts = rel.split('/');
        if (std::none_of(parts.begin(), parts.end(), [](const QString &p) { return p.startsWith('.'); }))   // anche su Windows
            found << rel;
    }
    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    for (const QString &rel : std::as_const(found))
        out.append({rel, rel.startsWith("docs/") ? QStringLiteral("docs") : rel.startsWith("fonti/coda/") ? QStringLiteral("coda")
                         : QFileInfo(QDir(dir).filePath(rel)).size() == 0 ? QStringLiteral("lascia")   // vuoto: non è materiale
                         : Pages::kindOf(rel) == "altro" ? QStringLiteral("docs") : QStringLiteral("coda")});
    return out;
}

QString Project::create(const QString &dir, const QHash<QString, QString> &placement, QStringList *moved)
{
    const QDir d(dir);
    if (d.exists() && !d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot) && placement.isEmpty())
        return QStringLiteral("La cartella non è vuota.");
    if (!QDir().mkpath(dir))
        return QStringLiteral("Non riesco a creare la cartella.");
    // i file che c'erano: in arrivo, da consultare (con le loro cartelle) o dove sono
    QStringList left;
    for (const auto &[rel, proposed] : loose(dir)) {
        const QString where = placement.value(rel, QStringLiteral("lascia"));
        if ((where == "docs" && rel.startsWith("docs/")) || (where == "coda" && rel.startsWith("fonti/coda/")) || where == "lascia")
            continue;                                  // è già dove va
        const QString to = where == "coda" ? files::freeName(d.filePath("fonti/coda/" + rel.split('/').join(QStringLiteral(" - "))))   // il nome dice da quale cartella viene
                                           : files::freeName(d.filePath("docs/" + rel));
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QDir().rename(d.filePath(rel), to)) {
            left << rel;
            continue;
        }
        if (moved)
            *moved << QStringLiteral("%1 → %2").arg(rel, d.relativeFilePath(to));
        // le cartelle rimaste vuote, risalendo fino alla radice
        for (QString folder = QFileInfo(rel).path(); folder != "." && !folder.isEmpty(); folder = QFileInfo(folder).path())
            if (!d.rmdir(folder))
                break;
    }
    const Project project(dir);
    QDir().mkpath(project.path("salvataggi"));
    if (!QFileInfo::exists(project.path(PROFILE))) {
        QFile::copy(":/profili/neutro.profilo.md", project.path(PROFILE));
        QFile::setPermissions(project.path(PROFILE), QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    project.complete();
    if (!isProject(dir))
        return QStringLiteral("Non riesco a scrivere nella cartella.");
    return left.isEmpty() ? QString() : QStringLiteral("Non sono riuscito a spostare %1: sono rimasti dov'erano.").arg(left.join(", "));
}

QStringList Project::complete() const
{
    QStringList added;
    for (const char *dir : {"src", "imgs", "fonti/coda", "fonti/usate", "docs", "salvataggi"})
        if (!QFileInfo(path(dir)).isDir() && QDir().mkpath(path(dir)))
            added << QString::fromUtf8(dir) + '/';

    auto ensure = [&](const char *file, const QString &text) {
        if (!QFileInfo::exists(path(file)) && files::write(path(file), text))
            added << QString::fromUtf8(file);
    };
    ensure(MAIN, QString::fromUtf8(MAIN_TEMPLATE).replace("@TITOLO@", name()));
    if (!QFileInfo::exists(path("latexmkrc")))
        ensure(".latexmkrc", QString::fromUtf8(LATEXMKRC));
    ensure(STATE, QString::fromUtf8(QJsonDocument(QJsonObject{{"formato", FORMAT}}).toJson()));

    // ogni progetto ha uno stile: uno di prima, senza, prende lo stile predefinito delle materie scientifiche, che è il
    // documento classico di allora (gli stessi ambienti: le lezioni non cambiano)
    QJsonObject state = QJsonDocument::fromJson(files::read(path(STATE)).toUtf8()).object();
    if (state.value("stile").toString().isEmpty()) {
        const QString style = QString::fromUtf8(Project::DEFAULT_STYLE);
        bool copied = true;
        for (const QString &file : {style + ".sty", style + ".stile.json"})
            if (!QFileInfo::exists(path(file)))
                copied = QFile::copy(":/stili/" + file, path(file)) && QFile::setPermissions(path(file), QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther) && copied;
        if (copied) {
            state.insert("stile", style);
            if (files::write(path(STATE), QString::fromUtf8(QJsonDocument(state).toJson())))
                added << style + ".sty";
        }
    }
    // un progetto di prima, senza profilo: le regole con cui è stato scritto (myprofile), con le sue in più
    if (!QFileInfo::exists(path(PROFILE))) {
        rules::Profile mine = rules::Profile::parse(files::read(":/profili/myprofile.profilo.md"));
        for (const QJsonValue &rule : state.value("regole").toArray())
            if (!rule.toString().trimmed().isEmpty())
                mine.extra << rule.toString().trimmed();
        if (files::write(path(PROFILE), mine.markdown()))
            added << QString::fromUtf8(PROFILE);
    }
    // texfix.sty è del programma: se non è quello di questa versione e di questo stile, lo riscrive
    QStringList packages;
    for (const QJsonValue &name : state.value("pacchetti").toArray())
        packages << name.toString();
    QStringList carried;
    for (const QJsonValue &line : state.value("macro_portate").toArray())
        carried << line.toString();
    const QString package = packageText(state.value("stile").toString(), packages, carried, !state.value("argomenti").toObject().isEmpty());
    if (files::read(path(PACKAGE)) != package && files::write(path(PACKAGE), package))
        added << QString::fromUtf8(PACKAGE);

    if (!hasRepository()) {
        ensure(".gitignore", QString::fromUtf8(GITIGNORE));
        QProcess git;
        git.setProcessEnvironment(tools::environment());
        git.setWorkingDirectory(m_root);
        tools::start(git, "git", {"init", "--quiet"});
        if (git.waitForFinished(10000) && git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0)
            added << QStringLiteral("il repository");
    }
    return added;
}

QString Project::packageText(const QString &style, const QStringList &packages, const QStringList &carried, bool byTopic)
{
    QString middle = QStringLiteral("\n% Lo stile del progetto\n\\RequirePackage{%1}\n").arg(style.isEmpty() ? QString::fromUtf8(Project::DEFAULT_STYLE) : style);
    // le figure: TikZ e pgfplots ci sono sempre, i pacchetti che un disegno chiede si aggiungono
    middle += QStringLiteral("\n% Le figure\n\\RequirePackage{tikz,pgfplots}\n\\usetikzlibrary{arrows.meta,calc,angles,quotes,intersections}\n"
                             "\\pgfplotsset{compat=1.18}\n");
    for (const QString &package : packages)
        middle += QStringLiteral("\\RequirePackage{%1}\n").arg(package);
    if (!carried.isEmpty()) {                         // dopo lo stile: chi le definisce già vince
        middle += QStringLiteral("\n% Macro dello stile di prima che le lezioni usano ancora\n");
        for (const QString &line : carried)
            middle += line + '\n';
    }
    QString tail = QString::fromUtf8(PACKAGE_TAIL);
    if (byTopic)                                       // per argomento: dove comincia il materiale di un giorno, solo la data (se c'è)
        tail.replace(QStringLiteral("\\if\\relax\\detokenize{#1}\\relax\\else Lezione~#1\\if\\relax\\detokenize{#2}\\relax\\else\\ --\\ \\fi\\fi#2"),
                     QStringLiteral("#2"))
            .replace(QStringLiteral("\\providecommand{\\lezione}[2]{\\par\\noindent\\textbf{%"),
                     QStringLiteral("\\providecommand{\\lezione}[2]{\\if\\relax\\detokenize{#2}\\relax\\else\\par\\noindent\\textit{%"));
    if (byTopic)
        tail.replace(QStringLiteral("#2}\\par\\nobreak}"), QStringLiteral("#2}\\par\\nobreak\\fi}"));
    return QString::fromUtf8(PACKAGE_HEAD) + middle + tail;
}

QStringList Project::inputs() const { return inputsOf(files::read(path(MAIN))); }

QStringList Project::inputsOf(const QString &mainText)
{
    static const QRegularExpression input(R"(\\input\{\s*(src/[^}\s]+)\s*\})");
    QStringList names;
    bool body = false;
    for (QStringView raw : QStringView(mainText).split('\n')) {
        QStringView line = uncommented(raw);
        if (!body) {
            const qsizetype at = line.indexOf(QLatin1String("\\begin{document}"));
            if (at < 0)
                continue;
            body = true;
            line = line.mid(at);
        }
        for (auto it = input.globalMatchView(line); it.hasNext();) {
            const QString name = it.next().captured(1);
            names << (name.endsWith(".tex") ? name : name + ".tex");
        }
    }
    return names;
}

QList<Lesson> Project::lessons() const
{
    QList<Lesson> out;
    for (const QString &file : inputs()) {
        bool ok = false;
        const QString text = files::read(path(file), &ok);
        if (ok)
            out << lessonOf(file, text);
    }
    return out;
}

Lesson Project::lessonOf(const QString &file, const QString &text)
{
    static const QRegularExpression mark(R"(\\lezione\s*\{\s*(\d*)\s*\}\s*\{([^}]*)\})");
    static const QRegularExpression section(R"(\\section\*?\s*(?:\[[^\]]*\])?\s*\{)");
    Lesson lesson;
    lesson.file = file;
    const QString clean = withoutComments(text);
    if (const auto m = mark.match(clean); m.hasMatch()) {
        lesson.number = m.captured(1).toInt();
        lesson.date = m.captured(2).trimmed();
    }
    if (const auto m = section.match(clean); m.hasMatch())
        lesson.title = braced(clean, m.capturedEnd() - 1).simplified();
    return lesson;
}

bool Project::hasRepository() const
{
    QDir d(m_root);
    do {
        if (QFileInfo::exists(d.filePath(".git")))
            return true;
    } while (d.cdUp());
    return false;
}

}

namespace tx::fileops {

namespace {
// Toglie o rinomina l'\input di una lezione nel documento (con il \clearpage che lo segue, se si toglie).
void updateMain(const Project &project, const QString &lesson, const QString &replacement)
{
    QStringList lines = files::read(project.path(Project::MAIN)).split('\n');
    const QString stem = lesson.chopped(4);
    static const QRegularExpression input(R"(\\input\{\s*(src/[^}\s]+)\s*\})");
    for (int i = 0; i < lines.size(); ++i) {
        const auto m = input.match(uncommented(lines[i]).toString());
        if (!m.hasMatch() || (m.captured(1) != lesson && m.captured(1) != stem))
            continue;
        if (!replacement.isEmpty()) {
            lines[i].replace(m.captured(0), QStringLiteral("\\input{%1}").arg(replacement));
            continue;
        }
        lines.removeAt(i);
        if (i < lines.size() && lines[i].trimmed() == "\\clearpage")
            lines.removeAt(i);
        --i;
    }
    files::write(project.path(Project::MAIN), lines.join('\n'));
}
}

QString rename(const Project &project, const QString &file, const QString &newName)
{
    QString name = newName.trimmed();
    if (name.isEmpty() || name.contains('/'))
        return QStringLiteral("Serve un nome, senza barre.");
    const QFileInfo info(project.path(file));
    if (!info.suffix().isEmpty() && QFileInfo(name).suffix().isEmpty())
        name += '.' + info.suffix();                   // l'estensione resta, se non la si scrive
    const QString target = info.path() + '/' + name;
    if (QFileInfo::exists(target))
        return QStringLiteral("«%1» c'è già.").arg(name);
    const bool lesson = project.inputs().contains(file);
    if (!QFile::rename(info.filePath(), target))
        return QStringLiteral("Non riesco a rinominare il file.");
    if (lesson)
        updateMain(project, file, project.relative(target));
    return {};
}

QString duplicate(const Project &project, const QString &file)
{
    const QString target = files::freeName(project.path(file));
    return QFile::copy(project.path(file), target) ? project.relative(target) : QString();
}

QString trash(const Project &project, const QString &file)
{
    const bool lesson = project.inputs().contains(file);
    if (!QFile::moveToTrash(project.path(file)))
        return QStringLiteral("Non riesco a spostare il file nel Cestino.");
    if (lesson)
        updateMain(project, file, {});
    return {};
}

}
