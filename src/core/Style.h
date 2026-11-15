#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace tx {

// Lo stile di un progetto, come Texfix lo conosce: il catalogo degli ambienti con il loro ruolo, le macro, come si
// fanno figure e rimandi, e le convenzioni che lo stile chiede a chi scrive. Senza stile vale `classic()`: article e
// amsthm. Di uno stile importato la descrizione sta accanto al .sty, in <nome>.stile.json.
struct Style {
    struct Environment {
        QString name;
        QString role;                      // uno di roles(); vuoto: non ancora deciso (una domanda all'utente)
        QString title = "opzionale";       // nessuno | opzionale: \begin{nome}[Titolo]
        QString color;                     // «#rrggbb» nel PDF; vuoto: nessuno
        bool breaks = true;                // può spezzarsi fra una pagina e l'altra
        QString note;                      // ciò che serve sapere per usarlo
        bool operator==(const Environment &) const = default;
    };
    struct Macro {
        QString name;                      // «\R»
        QString meaning;
        bool operator==(const Macro &) const = default;
    };

    QString name;                          // vuoto: nessuno stile
    QList<Environment> environments;
    QList<Macro> macros;
    QString references = "cref";           // come si scrivono i rimandi: cref | tag (\tagref)
    QString figureEnvironment;             // l'ambiente in cui sta una figura; vuoto: figure con \centering
    QString captionCommand;                // il comando della didascalia; vuoto: \caption
    QString conventions;                   // ciò che lo stile chiede oltre il catalogo, in markdown
    QString classOptions;                  // le opzioni di \documentclass che lo stile vuole, separate da virgole (twocolumn)
    bool twoColumns() const { return classOptions.contains("twocolumn"); }

    bool operator==(const Style &) const = default;

    // I ruoli che Texfix conosce: valore e nome per l'utente.
    struct Role {
        QString value, label;
    };
    static QList<Role> roles();

    static Style classic();
    static QString descriptionFile(const QString &name) { return name + ".stile.json"; }
    static Style load(const QString &path, bool *ok = nullptr);
    bool save(const QString &path) const;

    // Una prima descrizione letta dal .sty, senza modello: ambienti, macro e colori trovati, con il ruolo proposto
    // dal nome dove si capisce. Ciò che resta senza ruolo è una domanda.
    static Style scan(const QString &name, const QString &styText);

    const Environment *environment(const QString &name) const;
    QStringList boxes() const;                                         // gli ambienti che sono unità di una lezione
    QString figureWrapper() const;                                     // l'ambiente in cui sta una figura
    QString environmentFor(const QString &role) const;                  // vuoto se lo stile non ne ha
    QStringList undecided() const;                                      // gli ambienti senza ruolo
    // Le convenzioni dello stile per un prompt: gli ambienti per ruolo, le macro, rimandi e figure, poi `conventions`.
    QString forPrompt() const;
};

}
