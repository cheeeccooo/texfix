#pragma once
#include <QColor>
#include <QString>

class QPainter;

// Le formule disegnate nel programma con MicroTeX, come in Studify: una formula si legge una volta (poi resta in
// memoria) e si misura e si disegna subito, nel colore e alla dimensione del testo intorno, con i font Computer Modern
// di MicroTeX. Si usa solo dal thread della finestra (o da quello che disegna mentre la finestra aspetta).
namespace math {

struct Size {
    qreal width = 0, height = 0;
    qreal ascent = 0;                                  // dalla cima alla linea di base
    bool ok = false;                                   // falso: la formula non si legge (si mostra il sorgente)
};

// Misura una formula; display: a sé (frazioni grandi, limiti sopra e sotto), se no in linea.
Size measure(const QString &latex, qreal size, bool display = false);
// La disegna con il bordo sinistro in x e la linea di base in baseline.
void draw(QPainter *painter, const QString &latex, qreal x, qreal baseline, qreal size, const QColor &color, bool display = false);

}
