#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace tx {

// Una lezione: un file di src/ richiamato dal documento.
struct Lesson {
    QString file;                          // «src/src3.tex», relativo alla radice
    int number = 0;                        // da \lezione{N}{data}; 0 se non c'è
    QString date;
    QString title;                         // il primo \section
};

// Un progetto di appunti: una cartella che si legge e si compila anche senza Texfix (`latexmk` dalla radice).
//   main.tex            il documento: preambolo, e gli \input delle lezioni
//   texfix.sty          ciò che Texfix aggiunge al documento; lo scrive il programma, per intero
//   .latexmkrc          creato se manca; quello dell'utente non si tocca
//   src/ imgs/          le lezioni e le loro immagini
//   fonti/coda|usate/   le fonti da convertire e quelle già usate
//   docs/               documenti di consultazione dell'utente: nessuno li cambia
//   salvataggi/         ciò che Texfix ricorda del progetto; progetto.json dice che la cartella è un progetto
//   build/              tutto ciò che si rifà
class Project {
public:
    static constexpr const char *MAIN = "main.tex";
    static constexpr const char *PACKAGE = "texfix.sty";
    // Lo stile di un progetto che non ne ha uno (quelli di prima): il documento classico, article con amsthm.
    static constexpr const char *DEFAULT_STYLE = "science_default";
    // Il profilo del progetto, la sua copia (core/Profiles). Un progetto nuovo parte dal Neutro; uno di prima, senza,
    // riceve all'apertura «myprofile» (le regole con cui è stato scritto) con le sue regole del progetto in più.
    static constexpr const char *PROFILE = "salvataggi/profilo.md";
    static constexpr const char *STATE = "salvataggi/progetto.json";
    static constexpr int FORMAT = 1;

    Project() = default;
    explicit Project(const QString &root);

    bool isValid() const { return !m_root.isEmpty(); }
    const QString &root() const { return m_root; }
    QString name() const;
    QString path(const QString &relative) const { return m_root + '/' + relative; }
    QString relative(const QString &absolute) const;

    static bool isProject(const QString &dir);
    // I file di una cartella che sta per diventare un progetto (relativi, senza i nascosti, in ordine), ognuno con dove
    // lo si propone: «coda» ciò che Texfix sa leggere (Pages::kindOf), «docs» il resto; ciò che è già in docs/ o in
    // fonti/coda/ resta lì.
    static QList<std::pair<QString, QString>> loose(const QString &dir);
    // Una cartella diventa un progetto intero. Vuota (o che ancora non c'è), o con dei file: placement dice dove va
    // ognuno, «coda» (in arrivo: fonti/coda/, un nome già preso diventa «nome (2)»), «docs» (da consultare, con le sue
    // cartelle) o «lascia»; senza placement una cartella con dei file non si tocca. I file nascosti restano dove sono, e
    // le cartelle che restano vuote si tolgono. Vuoto se è andata; altrimenti il perché. moved: che cosa ha spostato.
    static QString create(const QString &dir, const QHash<QString, QString> &placement = {}, QStringList *moved = nullptr);
    // Aggiunge ciò che manca (cartelle, main.tex, texfix.sty, .latexmkrc, il repository) senza toccare ciò che c'è.
    // Restituisce che cosa ha aggiunto.
    QStringList complete() const;
    // Il testo di texfix.sty: lo stile del progetto (o, senza stile, il classico amsthm) e ciò che è di Texfix.
    // byTopic: per argomento \lezione stampa solo la data, se c'è (il numero di lezione non dice niente in un argomento)
    static QString packageText(const QString &style, const QStringList &packages = {}, const QStringList &carried = {}, bool byTopic = false);

    // Le lezioni sono una regola sola: gli \input{src/…} non commentati dopo \begin{document} di main.tex, in
    // ordine, sempre con «.tex». Anche quelli che mancano su disco: chi li usa decide.
    QStringList inputs() const;
    static QStringList inputsOf(const QString &mainText);
    // Le lezioni che ci sono su disco, con numero, data e titolo letti dal file.
    QList<Lesson> lessons() const;
    static Lesson lessonOf(const QString &file, const QString &text);

    bool hasRepository() const;            // la cartella, o una sopra, è un repository git

private:
    QString m_root;
};

// Il testo di una riga senza il suo commento (un % non preceduto da \).
QStringView uncommented(QStringView line);

}

namespace tx::fileops {
// Rinomina un file del progetto (percorsi relativi). Se è una lezione, il documento la segue: il suo \input cambia
// nome. Vuoto se è andata; altrimenti il perché.
QString rename(const Project &project, const QString &file, const QString &newName);
// Una copia accanto all'originale, «nome (2).tex». Restituisce il file nuovo (vuoto se non è andata).
QString duplicate(const Project &project, const QString &file);
// Nel Cestino. Una lezione esce anche dal documento.
QString trash(const Project &project, const QString &file);
}
