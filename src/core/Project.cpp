#include "core/Project.h"

#include "core/Files.h"
#include "core/Tools.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

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

// Senza stile: un classico documento matematico, article e amsthm, nessun colore.
const char *const PACKAGE_CLASSIC = R"(
% Nessuno stile: teoremi, definizioni ed esempi con amsthm
\RequirePackage{amsmath,amssymb,amsthm,mathtools}
\RequirePackage{graphicx}
\RequirePackage{hyperref}
\RequirePackage{cleveref}
\theoremstyle{plain}
\newtheorem{theorem}{Teorema}[section]
\newtheorem{proposition}[theorem]{Proposizione}
\newtheorem{lemma}[theorem]{Lemma}
\newtheorem{corollary}[theorem]{Corollario}
\theoremstyle{definition}
\newtheorem{definition}[theorem]{Definizione}
\newtheorem{example}[theorem]{Esempio}
\newtheorem{exercise}[theorem]{Esercizio}
\theoremstyle{remark}
\newtheorem{remark}[theorem]{Osservazione}
)";

const char *const PACKAGE_TAIL = R"(
% Impaginazione: un paragrafo non lascia una riga sola in fondo o in cima alla pagina, né prima di una formula
\clubpenalty=10000 \widowpenalty=10000 \displaywidowpenalty=10000
% (dove sta il testo nella pagina, scritto nel log: serve a misurare i vuoti)
\AtBeginDocument{\typeout{TEXFIX pagina: alto \the\dimexpr 1in+\voffset+\topmargin+\headheight+\headsep\relax, testo \the\textheight}}

% Aggiunte e correzioni dichiarate da chi ha scritto gli appunti (se lo stile non ha le sue)
\@ifundefined{addedtext}{\newenvironment{addedtext}{\par\begingroup\small\noindent\textbf{[+]}\ \ignorespaces}{\par\endgroup}}{}
\@ifundefined{correction}{\newenvironment{correction}{\par\begingroup\small\noindent\textbf{[!]}\ \ignorespaces}{\par\endgroup}}{}

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

QString Project::create(const QString &dir)
{
    const QDir d(dir);
    if (d.exists() && !d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot))
        return QStringLiteral("La cartella non è vuota.");
    if (!QDir().mkpath(dir))
        return QStringLiteral("Non riesco a creare la cartella.");
    Project(dir).complete();
    return isProject(dir) ? QString() : QStringLiteral("Non riesco a scrivere nella cartella.");
}

QStringList Project::adopt(const QString &dir)
{
    QStringList moved;
    const QDir d(dir);
    const QStringList entries = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);   // senza i nascosti
    QDir().mkpath(dir + "/docs");
    for (const QString &entry : entries) {
        if (entry == "docs")
            continue;                                   // ciò che era già in docs/ resta lì
        const QString to = files::freeName(dir + "/docs/" + entry);
        if (QDir().rename(d.filePath(entry), to))
            moved << QStringLiteral("%1 → docs/%2").arg(entry, QFileInfo(to).fileName());
    }
    Project(dir).complete();
    return moved;
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

    // texfix.sty è del programma: se non è quello di questa versione e di questo stile, lo riscrive
    const QJsonObject state = QJsonDocument::fromJson(files::read(path(STATE)).toUtf8()).object();
    QStringList packages;
    for (const QJsonValue &name : state.value("pacchetti").toArray())
        packages << name.toString();
    const QString package = packageText(state.value("stile").toString(), packages);
    if (files::read(path(PACKAGE)) != package && files::write(path(PACKAGE), package))
        added << QString::fromUtf8(PACKAGE);

    if (!hasRepository()) {
        ensure(".gitignore", QString::fromUtf8(GITIGNORE));
        QProcess git;
        git.setProcessEnvironment(tools::environment());
        git.setWorkingDirectory(m_root);
        git.start("git", {"init", "--quiet"});
        if (git.waitForFinished(10000) && git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0)
            added << QStringLiteral("il repository");
    }
    return added;
}

QString Project::packageText(const QString &style, const QStringList &packages)
{
    QString middle = style.isEmpty() ? QString::fromUtf8(PACKAGE_CLASSIC)
                                     : QStringLiteral("\n% Lo stile del progetto\n\\RequirePackage{%1}\n").arg(style);
    // le figure: TikZ e pgfplots ci sono sempre, i pacchetti che un disegno chiede si aggiungono
    middle += QStringLiteral("\n% Le figure\n\\RequirePackage{tikz,pgfplots}\n\\usetikzlibrary{arrows.meta,calc,angles,quotes,intersections}\n"
                             "\\pgfplotsset{compat=1.18}\n");
    for (const QString &package : packages)
        middle += QStringLiteral("\\RequirePackage{%1}\n").arg(package);
    return QString::fromUtf8(PACKAGE_HEAD) + middle + QString::fromUtf8(PACKAGE_TAIL);
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
