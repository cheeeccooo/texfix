#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace tx {

// La cronologia del progetto: l'unico modo di tornare indietro (⌘Z, ⇧⌘Z), per i lavori, le decisioni e le modifiche
// a mano. Ogni passo è lo stato dei file seguiti; annullare riporta i file al passo prima, ripetere a quello dopo,
// un passo nuovo dopo un annullamento toglie quelli avanti.
//
// Vale finché il progetto è aperto: i passi stanno in memoria, i contenuti una volta sola in build/cronologia/, che
// si butta alla chiusura (e all'apertura, se è rimasta da una chiusura sbagliata).
//
// Si seguono: main.tex, .latexmkrc, i .sty della radice, src/, imgs/, fonti/ e salvataggi/ (tranne chat, lavori,
// esiti). Non docs/ né build/.
class History {
public:
    struct Step {
        QString title, when;
    };
    struct Outcome {
        bool ok = false;
        QString message;                               // per l'utente
        QStringList changed;                           // i file scritti o tolti
    };
    static constexpr int KEEP = 200;                   // i passi tenuti: i più vecchi si dimenticano

    explicit History(const QString &root);             // il primo passo: com'è il progetto adesso
    ~History();
    History(const History &) = delete;
    History &operator=(const History &) = delete;

    // Un passo nuovo, se i file sono cambiati dall'ultimo. true se l'ha registrato.
    bool record(const QString &title);
    Outcome undo();                                    // con modifiche a mano non ancora in un passo, annulla quelle
    Outcome redo();
    Outcome goTo(int index);

    QList<Step> steps() const;
    QString digest();                                  // un'impronta dello stato dei file seguiti adesso
    void renameFirst(const QString &title);            // il primo passo: «Com'era all'apertura» o «Modifiche fuori da Texfix»
    int cursor() const { return m_cursor; }
    // I titoli per il menu: vuoto se non c'è niente da annullare o da ripetere.
    QString undoTitle();
    QString redoTitle();

    // Una copia dei file seguiti che resta anche dopo la chiusura, in una cartella data: un lavoro intero si annulla da lì
    // («Annulla tutto il lavoro»). restoreCopy riporta i file com'erano (only: solo questi) e toglie quelli seguiti che non
    // c'erano; restituisce i file scritti o tolti.
    static bool saveCopy(const QString &root, const QString &dir);
    static QStringList restoreCopy(const QString &root, const QString &dir, const QStringList &only = {});

private:
    using State = QHash<QString, QString>;             // file → impronta del contenuto
    struct Entry {
        QString title, when;
        State files;
    };
    struct Seen {                                      // un file già letto: si rilegge solo se cambia
        qint64 size = 0, modified = 0;
        QString hash;
    };

    State scan();
    void store(const State &state) const;
    QStringList restore(const State &target, const State &now) const;
    bool append(const State &now, const QString &title);
    Outcome jump(const State &now, int target, const QString &what, bool back);
    QString objectPath(const QString &hash) const;
    void forget();                                     // oltre KEEP: via i passi più vecchi e i contenuti di nessuno

    QString m_root;
    QList<Entry> m_steps;
    int m_cursor = -1;
    QHash<QString, Seen> m_seen;
};

}
