import QtQuick
import QtQuick.Layouts
import Texfix

// L'esito corto di un lavoro: una riga per lezione, il costo e i minuti.
TxSheet {
    id: root
    wantedWidth: 560
    property var outcome: ({ title: "", lines: [], cost: "", minutes: "" })
    title: outcome.title
    function show(o) { outcome = o; open() }

    Repeater {
        model: root.outcome.lines
        TxLabel {
            required property string modelData
            Layout.fillWidth: true
            text: modelData
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }
    TxLabel {
        Layout.fillWidth: true
        Layout.topMargin: Theme.gap
        text: "Costo: " + root.outcome.cost + " · " + root.outcome.minutes + " minuti"
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
    }
    buttons: TxButton { text: "Chiudi"; primary: true; onClicked: root.close() }
}
