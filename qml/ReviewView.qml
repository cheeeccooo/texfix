import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Rivedi dubbi e Rivedi correzioni: la stessa vista, lezione per lezione. Con «Riepilogo» anche ciò che è già deciso,
// con il suo come, per cambiare idea.
Rectangle {
    id: root
    required property string section                   // dubbi | correzioni
    property bool summary: false
    property var items: []
    signal lineAsked(string file, int line)
    color: Theme.window

    function reload() { items = Workspace.reviewItems(section, summary) }
    Component.onCompleted: reload()
    onSummaryChanged: reload()
    onVisibleChanged: if (visible) reload()            // si rilegge quando torna in vista
    Connections { target: Workspace; function onFilesChanged() { if (root.visible) root.reload() } }

    // Porta la vista su una lezione (dall'albero): una lezione fatta, o già tutta decisa, sta nel riepilogo.
    function showLesson(lesson) {
        if (!items.some(i => i.type === "lesson" && i.lesson === lesson))
            summary = true
        for (let i = 0; i < items.length; ++i)
            if (items[i].type === "lesson" && items[i].lesson === lesson)
                return list.positionViewAtIndex(i, ListView.Beginning)
    }

    PdfBackend { id: pdf }                             // per l'indirizzo delle pagine delle fonti

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            spacing: Theme.gap
            TxLabel {
                Layout.fillWidth: true
                text: (root.summary ? "Riepilogo " : "Rivedi ") + root.section
                font.pixelSize: Theme.textSize + 3
                font.weight: Font.DemiBold
            }
            TxButton { text: root.summary ? "Solo da decidere" : "Riepilogo"; onClicked: root.summary = !root.summary }
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
                id: slot
                required property var modelData
                x: Theme.gap + 4
                width: Math.min(list.width - 2 * Theme.gap - 8, 860)
                sourceComponent: modelData.type === "lesson" ? lessonHead : modelData.type === "doubt" ? doubtCard : declaredCard
                onLoaded: item.row = modelData
            }
        }
    }
    TxLabel {
        anchors.centerIn: parent
        visible: root.items.length === 0
        text: root.summary ? "Non c'è niente." : "Niente da decidere."
        color: Theme.textFaint
    }

    component Card: Rectangle {
        default property alias content: column.data
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
        }
    }

    Component {
        id: lessonHead
        RowLayout {
            property var row: ({})
            spacing: Theme.gap
            TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: row.title || ""; font.weight: Font.DemiBold }
            TxLabel { visible: row.done === true; text: "fatta"; color: Theme.good; font.pixelSize: Theme.smallSize }
            TxButton {
                visible: root.section === "dubbi" && row.open > 0
                text: "Fai decidere al modello tutti i dubbi"
                enabled: !Workspace.busy
                onClicked: Workspace.decideDoubts(row.lesson, [])
            }
            TxButton {
                visible: root.section === "correzioni" && row.open > 1
                text: "Tieni tutto"
                onClicked: Workspace.keepAllDeclared(row.lesson)
            }
        }
    }

    Component {
        id: doubtCard
        Card {
            id: card
            property var row: ({ readings: [] })
            property bool showSource: false
            TxLabel { Layout.fillWidth: true; text: card.row.what || ""; font.weight: Font.Medium; wrapMode: Text.WordWrap; elide: Text.ElideNone }
            // la pagina della fonte
            Image {
                Layout.fillWidth: true
                Layout.preferredHeight: status === Image.Ready ? Math.min(width * sourceSize.height / sourceSize.width, 420) : 0
                visible: source != ""
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                asynchronous: true
                source: card.row.sourcePath ? "image://pdf/0/" + (Math.max(1, card.row.page) - 1) + "/1400/" + encodeURIComponent(card.row.sourcePath) : ""
            }
            // da confermare: il prima e il dopo di una correzione a mano, o il perché del modello
            ColumnLayout {
                Layout.fillWidth: true
                visible: card.row.pending === true
                spacing: 2
                TxLabel {
                    Layout.fillWidth: true
                    text: card.row.how === "modello" ? "Il modello: " + (card.row.why || "") + " Scritto ora: " + (card.row.latex || "")
                          : (card.row.before || []).length + (card.row.after || []).length === 0 ? "Correggi il passo nella lezione e salva: qui compare il prima e il dopo." : "Corretto da te:"
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                Repeater {
                    model: card.row.before || []
                    TxLabel { required property string modelData; Layout.fillWidth: true; text: "− " + modelData; color: Theme.bad; font.family: Theme.monoFamily; wrapMode: Text.WrapAnywhere; elide: Text.ElideNone }
                }
                Repeater {
                    model: card.row.after || []
                    TxLabel { required property string modelData; Layout.fillWidth: true; text: "+ " + modelData; color: Theme.good; font.family: Theme.monoFamily; wrapMode: Text.WrapAnywhere; elide: Text.ElideNone }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.gap
                    TxButton { visible: card.row.how === "corretta"; text: "Modifica"; onClicked: root.lineAsked(card.row.lesson, Math.max(1, card.row.line)) }
                    TxButton { text: "Riapri"; flat: true; onClicked: Workspace.reopenDoubt(card.row.lesson, card.row.id, card.row.how === "modello") }
                    TxButton { text: "Conferma"; primary: true; onClicked: Workspace.confirmDoubt(card.row.lesson, card.row.id) }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gap
                visible: card.row.pending !== true
                Repeater {
                    model: card.row.readings
                    TxButton {
                        required property var modelData
                        required property int index
                        readonly property bool mine: !card.row.open && card.row.chosen === index
                        text: (mine ? "✓ " : "") + modelData.text + (index === 0 && card.row.open ? "  (com'è scritto)" : "")
                        primary: mine
                        width: Math.min(implicitWidth, card.width - 2 * Theme.gap - 4)
                        onClicked: Workspace.chooseReading(card.row.lesson, card.row.id, index)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TxLabel {
                    Layout.fillWidth: true
                    text: (card.row.source || "") + (card.row.page > 0 ? ", pagina " + card.row.page : "")
                          + (card.row.open ? "" : card.row.how === "confermata" ? " · ✓ Confermata" : card.row.how === "corretta" ? " · ✓ Corretto da te" : card.row.how === "modello" ? " · ✓ Deciso dal modello" : " · ✓ Scelta")
                    color: Theme.textFaint
                    font.pixelSize: Theme.smallSize
                }
                TxButton { visible: card.row.open === true && card.row.pending !== true; text: "Correggo io"; flat: true; onClicked: { Workspace.correctDoubt(card.row.lesson, card.row.id); root.lineAsked(card.row.lesson, Math.max(1, card.row.line)) } }
                TxButton { visible: card.row.open === true && card.row.pending !== true; text: "Decidi tu"; flat: true; enabled: !Workspace.busy; onClicked: Workspace.decideDoubts(card.row.lesson, [card.row.id]) }
                TxButton { text: "Sorgente"; flat: true; onClicked: card.showSource = !card.showSource }
                TxButton { visible: card.row.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(card.row.lesson, card.row.line) }
                TxButton { visible: card.row.open === false; text: "Riapri"; flat: true; onClicked: Workspace.reopenDoubt(card.row.lesson, card.row.id, false) }
                TxButton {
                    visible: card.row.open === false && card.row.how === "scelta"
                    text: "Annulla la modifica"
                    flat: true
                    onClicked: Workspace.reopenDoubt(card.row.lesson, card.row.id, true)
                }
            }
            // dopo una decisione diversa da ciò che era scritto: ricordarla come regola del progetto
            RowLayout {
                Layout.fillWidth: true
                visible: card.row.open === false && card.row.how !== "confermata"
                spacing: Theme.gap
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.rowHeight
                    radius: Theme.radius
                    color: Theme.window
                    border.width: 1
                    border.color: rule.activeFocus ? Theme.accent : Theme.border
                    TextInput {
                        id: rule
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gap
                        anchors.rightMargin: Theme.gap
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.text
                        font.pixelSize: Theme.smallSize + 1
                        selectByMouse: true
                        clip: true
                        TxLabel { anchors.fill: parent; visible: parent.text === ""; text: "Ricorda come regola del progetto…"; color: Theme.textFaint; font.pixelSize: Theme.smallSize + 1 }
                    }
                }
                TxButton { text: "Ricorda"; enabled: rule.text.trim() !== ""; onClicked: { Workspace.addProjectRule(rule.text); rule.text = "" } }
            }
            TxLabel {
                Layout.fillWidth: true
                visible: card.showSource
                text: card.row.latex || ""
                font.family: Theme.monoFamily
                color: Theme.textMuted
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideNone
            }
        }
    }

    Component {
        id: declaredCard
        Card {
            id: card
            property var row: ({})
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TxLabel { text: card.row.kind === "correction" ? "Correzione" : "Aggiunta"; font.weight: Font.Medium }
                TxLabel {
                    Layout.fillWidth: true
                    text: card.row.state === "tenuta" ? "✓ Tenuta" : card.row.state === "tolta" ? "✓ Tolta" : ""
                    color: Theme.textFaint
                    font.pixelSize: Theme.smallSize
                }
                TxButton { visible: card.row.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(card.row.lesson, card.row.line) }
            }
            TxLabel { Layout.fillWidth: true; text: card.row.text || ""; wrapMode: Text.WordWrap; elide: Text.ElideNone; opacity: card.row.state === "tolta" ? 0.55 : 1 }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.gap
                TxButton {
                    text: card.row.state === "tenuta" ? "✓ Tieni" : "Tieni"
                    primary: card.row.state === "tenuta"
                    onClicked: card.row.state === "tolta" ? Workspace.restoreDeclared(card.row.lesson, card.row.hash)
                                                          : Workspace.keepDeclared(card.row.lesson, card.row.hash, true)
                }
                TxButton {
                    text: card.row.state === "tolta" ? "✓ Togli" : "Togli"
                    primary: card.row.state === "tolta"
                    enabled: card.row.state !== "tolta"
                    onClicked: Workspace.dropDeclared(card.row.lesson, card.row.hash)
                }
                TxButton { visible: card.row.state === "tenuta"; text: "Riapri"; flat: true; onClicked: Workspace.keepDeclared(card.row.lesson, card.row.hash, false) }
            }
        }
    }
}
