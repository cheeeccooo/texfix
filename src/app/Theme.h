#pragma once
#include <QColor>
#include <QObject>
#include <QQmlEngine>

// L'unico posto con colori e misure, per QML e per ciò che disegna il C++ (editor, numeri di riga).
// Chiaro o scuro come il sistema; TEXFIX_TEMA=chiaro|scuro lo forza (per le prove).
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

#define TX_COLOR(name)                                                                                                 \
    Q_PROPERTY(QColor name READ name NOTIFY changed)                                                                   \
public:                                                                                                                \
    QColor name() const { return m_colors.name; }                                                                      \
private:

    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    TX_COLOR(window)                       // lo sfondo della finestra e delle barre
    TX_COLOR(panel)                        // i pannelli ai lati
    TX_COLOR(surface)                      // dove si scrive e si legge
    TX_COLOR(border)
    TX_COLOR(text)
    TX_COLOR(textMuted)
    TX_COLOR(textFaint)
    TX_COLOR(accent)
    TX_COLOR(onAccent)
    TX_COLOR(hover)
    TX_COLOR(selected)
    TX_COLOR(good)
    TX_COLOR(warn)
    TX_COLOR(bad)
    TX_COLOR(idle)
    TX_COLOR(currentLine)
    TX_COLOR(selection)
    TX_COLOR(pdfBackdrop)
    TX_COLOR(synCommand)
    TX_COLOR(synEnvironment)
    TX_COLOR(synMath)
    TX_COLOR(synComment)
    TX_COLOR(synBrace)
#undef TX_COLOR

    Q_PROPERTY(int gap READ gap CONSTANT)              // lo spazio di base fra le cose
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int textSize READ textSize CONSTANT)
    Q_PROPERTY(int smallSize READ smallSize CONSTANT)
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)
    Q_PROPERTY(int monoSize READ monoSize CONSTANT)

public:
    struct Colors {
        QColor window, panel, surface, border, text, textMuted, textFaint, accent, onAccent, hover, selected, good, warn,
            bad, idle, currentLine, selection, pdfBackdrop, synCommand, synEnvironment, synMath, synComment, synBrace;
    };

    static Theme *instance();
    // Un colore per distinguere le fonti (nel piano dei concetti): pochi, leggibili su tutti e due gli sfondi; si ripetono.
    Q_INVOKABLE QColor sourceColor(int index) const;
    // Le scorciatoie scritte come sul Mac («⇧⌘B»), come le chiama il sistema: su Linux e Windows «Ctrl+Maiusc+B».
    Q_INVOKABLE QString keys(const QString &text) const;
    // Un colore del documento (un pastello di un riquadro) come inchiostro leggibile sullo sfondo dell'editor.
    QColor readable(const QColor &color) const;
    static Theme *create(QQmlEngine *, QJSEngine *);

    bool dark() const { return m_dark; }
    int gap() const { return 8; }
    int radius() const { return 6; }
    int textSize() const { return 13; }
    int smallSize() const { return 11; }
    int rowHeight() const { return 26; }
    QString monoFamily() const { return QStringLiteral("Menlo"); }
    int monoSize() const { return 13; }

signals:
    void changed();

private:
    Theme();
    void apply();

    bool m_dark = false;
    Colors m_colors;
};
