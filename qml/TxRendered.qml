import QtQuick
import QtQuick.Layouts
import Texfix

// Un pezzo di LaTeX o di prosa del modello, disegnato subito (`LatexText`: le parole nel carattere dell'interfaccia,
// le formule con MicroTeX). L'altezza si sa appena si sa la larghezza: niente lampeggia e niente si sposta dopo.
// `mono`: il sorgente così com'è, in caratteri fissi (il prima e il dopo di una correzione).
Item {
    id: piece
    property string text
    property string kind: "prosa"                      // prosa | latex
    property bool mono: false
    property color textColor: Theme.text
    property int weight: Font.Normal
    property bool fit: true                            // sta nella larghezza che ha; altrimenti prende la sua
    property real zoom: 0.52                           // la dimensione rispetto al testo: 0.52 è il testo normale
    property real maxWidth: 480                        // senza `fit`: fin dove si allarga
    Layout.fillWidth: fit
    implicitWidth: mono ? source.implicitWidth : piece.fit ? drawn.implicitWidth : Math.min(drawn.implicitWidth, maxWidth)
    implicitHeight: mono ? source.implicitHeight : drawn.implicitHeight
    LatexText {
        id: drawn
        visible: !piece.mono
        width: piece.width
        text: piece.mono ? "" : piece.text
        latex: piece.kind === "latex"
        color: piece.textColor
        brokenColor: Theme.warn
        fontSize: Math.round(Theme.textSize * piece.zoom / 0.52)
        bold: piece.weight >= Font.DemiBold
    }
    TxLabel {
        id: source
        visible: piece.mono
        width: piece.width
        text: piece.mono ? piece.text : ""
        color: piece.textColor
        font.family: Theme.monoFamily
        wrapMode: Text.WrapAnywhere
        elide: Text.ElideNone
    }
}
