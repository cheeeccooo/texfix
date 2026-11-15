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

    // Porta la vista su una lezione (dall'albero).
    function showLesson(lesson) {
        for (let i = 0; i < items.length; ++i)
            if (items[i].type === "lesson" && items[i].lesson === lesson)
                return list.positionViewAtIndex(i, ListView.Beginning)
    }

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
            readonly property bool open: ["fatta", "proposta", "segnalata", "a_mano", "agente"].includes(row.state)
            readonly property bool noMove: ["segnalata", "a_mano", "agente", "risolta"].includes(row.state)   // nessuna mossa del programma
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
                        text: ({ fatta: "fatta da sola: da rivedere", proposta: "proposta: da decidere", segnalata: "da decidere", a_mano: "la sistemi tu",
                                 agente: "affidata all'agente", accettata: "✓ Accettata", risolta: "✓ Risolta" })[box.row.state] || "✓ Lasciata com'era"
                        color: box.open ? Theme.warn : Theme.textFaint
                        font.pixelSize: Theme.smallSize
                    }
                    TxButton { visible: box.row.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(box.row.lesson, box.row.line) }
                    TxButton {
                        visible: box.open
                        text: "A mano"
                        hint: "Apre la lezione al blocco: la sistemi tu, compili e torni qui"
                        onClicked: { Workspace.decideMove(box.row.lesson, box.row.block, "a_mano"); root.lineAsked(box.row.lesson, box.row.line) }
                    }
                    TxButton {
                        visible: box.open && box.row.state !== "agente"
                        text: "All'agente"
                        onClicked: { Workspace.decideMove(box.row.lesson, box.row.block, "agente") }
                    }
                    TxButton {
                        visible: box.row.state === "a_mano" || box.row.state === "agente"
                        text: "Risolta"
                        onClicked: Workspace.decideMove(box.row.lesson, box.row.block, "risolta")
                    }
                    TxButton {
                        visible: !box.noMove
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
