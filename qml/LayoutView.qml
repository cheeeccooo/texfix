import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Rivedi impaginazione: le mosse dell'impaginazione, lezione per lezione. Ognuna si accetta o si lascia com'era, e si
// cambia quando si vuole; ogni scelta va subito nei file.
Rectangle {
    id: root
    property var items: []
    signal lineAsked(string file, int line)
    color: Theme.window

    function reload() { items = Workspace.layoutItems() }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onFilesChanged() { if (root.visible) root.reload() } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            spacing: Theme.gap
            TxLabel { Layout.fillWidth: true; text: "Rivedi impaginazione"; font.pixelSize: Theme.textSize + 3; font.weight: Font.DemiBold }
            TxButton { text: "Impagina ciò che è nuovo"; enabled: !Workspace.busy; onClicked: Workspace.paginate() }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.gap
            bottomMargin: 2 * Theme.gap
            model: root.items
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                required property var modelData
                x: Theme.gap + 4
                width: Math.min(list.width - 2 * Theme.gap - 8, 860)
                sourceComponent: modelData.type === "lesson" ? head : card
                onLoaded: item.row = modelData
            }
        }
    }
    TxLabel { anchors.centerIn: parent; visible: root.items.length === 0; text: "Nessuna mossa di impaginazione."; color: Theme.textFaint }

    Component {
        id: head
        TxLabel { property var row: ({}); topPadding: Theme.gap; text: row.title || ""; font.weight: Font.DemiBold }
    }
    Component {
        id: card
        Rectangle {
            id: box
            property var row: ({})
            readonly property bool applied: row.state === "fatta" || row.state === "accettata"
            implicitHeight: column.implicitHeight + 2 * Theme.gap + 4
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: column
                anchors.fill: parent
                anchors.margins: Theme.gap + 2
                spacing: Theme.gap
                TxLabel { Layout.fillWidth: true; text: box.row.what || ""; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxLabel {
                        Layout.fillWidth: true
                        text: box.row.state === "fatta" ? "fatta da sola: da rivedere" : box.row.state === "proposta" ? "proposta: da decidere"
                              : box.row.state === "accettata" ? "✓ Accettata" : "✓ Lasciata com'era"
                        color: box.row.state === "fatta" || box.row.state === "proposta" ? Theme.warn : Theme.textFaint
                        font.pixelSize: Theme.smallSize
                    }
                    TxButton { visible: box.row.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(box.row.lesson, box.row.line) }
                    TxButton {
                        text: box.row.state === "accettata" ? "✓ Accetta" : box.row.state === "proposta" ? "A pagina nuova" : "Accetta"
                        primary: box.row.state === "accettata"
                        onClicked: Workspace.decideMove(box.row.lesson, box.row.block, "accettata")
                    }
                    TxButton {
                        text: box.row.state === "lasciata" ? "✓ Lascia com'era" : "Lascia com'era"
                        primary: box.row.state === "lasciata"
                        onClicked: Workspace.decideMove(box.row.lesson, box.row.block, "lasciata")
                    }
                }
            }
        }
    }
}
