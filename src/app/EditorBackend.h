#pragma once
#include <QObject>
#include <QQmlEngine>
#include <QQuickTextDocument>

class LatexHighlighter;

// Ciò che un editor QML (TextEdit) non fa da sé: l'evidenziazione del LaTeX e le righe.
class EditorBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY lineCountChanged)

public:
    explicit EditorBackend(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    int lineCount() const;

    // La riga (da 1) in cui sta una posizione del testo, e la posizione in cui comincia una riga.
    Q_INVOKABLE int lineOf(int position) const;
    Q_INVOKABLE int positionOf(int line) const;

signals:
    void documentChanged();
    void lineCountChanged();

private:
    QQuickTextDocument *m_document = nullptr;
    LatexHighlighter *m_highlighter = nullptr;
};
