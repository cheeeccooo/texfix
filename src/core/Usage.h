#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <functional>

// L'utilizzo di Claude, come lo dice il CLI a ogni chiamata (`stream-json`): una riga
// {"type":"rate_limit_event","rate_limit_info":{…,"unifiedWindows":{"five_hour":{utilization,resetsAt,status},
// "seven_day":{…}}}}. Model, Worker e la chat passano qui ogni riga; si tiene l'ultimo stato (anche fra un avvio e
// l'altro, fra i dati del programma: utilizzo.json) e, della finestra di cinque ore, i campioni recenti, per dire a che
// ritmo sale. Niente chiamate in più: si sa ciò che le chiamate fatte hanno detto.
namespace tx::usage {

struct Window {
    double used = -1;                      // da 0 a 1; -1 se non si sa
    QDateTime resets;                      // quando si azzera
    QString status;                        // allowed, allowed_warning, rejected
    bool known() const { return used >= 0; }
};

struct State {
    Window hours;                          // la finestra di cinque ore
    Window week;                           // quella di sette giorni
    QDateTime seen;                        // quando l'ha detto l'ultima chiamata
    bool known() const { return hours.known() || week.known(); }
    // il limite è raggiunto adesso (una finestra rifiuta e non si è ancora azzerata): fino a quando
    QDateTime blockedUntil() const;
};

// Una riga dell'uscita del CLI: se è un rate_limit_event, aggiorna lo stato. Sicura fra i thread.
void record(const QJsonObject &line);
State current();
// A che ritmo sale la finestra di cinque ore dai campioni presi da `since` in poi: quando arriverebbe al 100%.
// Non valido se i campioni sono troppo pochi o troppo vicini, o se non sale.
QDateTime runsOutAt(const QDateTime &since);
// Chi guarda (la finestra): chiamato, da qualunque thread, a ogni cambiamento.
void onChange(std::function<void()> callback);
// Il file dove si ricorda (per le prove); vuoto: utilizzo.json fra i dati del programma.
void setFile(const QString &path);

}
