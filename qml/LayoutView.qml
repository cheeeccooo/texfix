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
    property bool allShown: false                      // per le prove: tutte le miniature aperte
    function showShots() { allShown = true }
    signal manualAsked(string lesson, string block, int line, string what)   // risolvere a mano: la lezione al blocco, con le prove
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
                sourceComponent: modelData.type === "lesson" ? head : modelData.type === "todo" ? todo : card
                onLoaded: item.row = modelData
            }
        }
    }
    TxLabel { anchors.centerIn: parent; visible: root.items.length === 0; text: "Niente da impaginare e nessuna mossa da rivedere."; color: Theme.textFaint }

    // una lezione ancora da impaginare: aprire la revisione non la impagina da sola
    Component {
        id: todo
        RowLayout {
            property var row: ({})
            spacing: Theme.gap
            TxLabel {
                Layout.fillWidth: true
                text: row.fresh ? "Non ancora impaginata." : "Cambiata dopo l'ultima impaginazione: da rifare."
                color: Theme.warn
            }
        }
    }
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
            readonly property bool hasShots: (row.before || []).length + (row.after || []).length > 0
            property bool shown: root.allShown                 // le pagine del prima e del dopo sono aperte
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
                // un clic sul testo apre le pagine del prima e del dopo
                TxLabel {
                    Layout.fillWidth: true
                    text: (box.hasShots ? (box.shown ? "▾ " : "▸ ") : "") + (box.row.what || "")
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    MouseArea { anchors.fill: parent; enabled: box.hasShots; cursorShape: Qt.PointingHandCursor; onClicked: box.shown = !box.shown }
                }
                TxLabel {
                    Layout.fillWidth: true
                        text: ({ fatta: "fatta da sola: da rivedere", proposta: "proposta: da decidere", segnalata: "da decidere", a_mano: "la sistemi tu",
                                 agente: "affidata all'agente", accettata: "✓ Accettata", risolta: "✓ Risolta" })[box.row.state] || "✓ Lasciata com'era"
                    color: box.open ? Theme.warn : Theme.textFaint
                    font.pixelSize: Theme.smallSize
                }
                // le pagine in piccolo: com'era, e com'è venuta
                Flow {
                    Layout.fillWidth: true
                    visible: box.shown
                    spacing: 2 * Theme.gap
                    Repeater {
                        model: box.shown ? [{ title: "Prima", pages: box.row.before || [] }, { title: "Dopo", pages: box.row.after || [] }].filter(g => g.pages.length > 0) : []
                        Column {
                            id: group
                            required property var modelData
                            spacing: 4
                            TxLabel { text: group.modelData.title; color: Theme.textMuted; font.pixelSize: Theme.smallSize; font.capitalization: Font.AllUppercase }
                            Row {
                                spacing: 4
                                Repeater {
                                    model: group.modelData.pages
                                    Rectangle {
                                        id: sheet
                                        required property var modelData
                                        width: Math.max(90, Math.min(230, (box.width - 80) / 4))
                                        height: width * 1.414
                                        color: "white"
                                        border.width: 1
                                        border.color: Theme.border
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: "file://" + sheet.modelData.path
                                            cache: false
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                            mipmap: true
                                        }
                                        // il contorno del blocco: rosso com'era, verde com'è venuto
                                        Repeater {
                                            model: sheet.modelData.rects
                                            Rectangle {
                                                required property var modelData
                                                readonly property color tone: group.modelData.title === "Prima" ? Theme.bad : Theme.good
                                                x: modelData.x * sheet.width
                                                y: modelData.y * sheet.height
                                                width: modelData.w * sheet.width
                                                height: modelData.h * sheet.height
                                                color: Qt.rgba(tone.r, tone.g, tone.b, 0.10)
                                                border.width: 2
                                                border.color: tone
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    Item { Layout.fillWidth: true }
                    TxButton { visible: box.row.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(box.row.lesson, box.row.line) }
                    TxButton {
                        visible: box.open
                        text: "A mano"
                        hint: "Apre la lezione al blocco: la sistemi tu, compili e torni qui"
                        onClicked: { Workspace.decideMove(box.row.lesson, box.row.block, "a_mano"); root.manualAsked(box.row.lesson, box.row.block, box.row.line, box.row.what) }
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
