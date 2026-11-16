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

    // Si rilegge tutto, ma cambia solo ciò che è cambiato: una riga uguale a prima resta la stessa, così la sua scheda non
    // si rifà e la lista resta dov'è.
    function reload() {
        const fresh = Workspace.reviewItems(section, summary), old = items
        for (let i = 0; i < fresh.length && i < old.length; ++i)
            if (JSON.stringify(fresh[i]) === JSON.stringify(old[i]))
                fresh[i] = old[i]
        items = fresh
    }
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

    // Le rese compilate: quando ne arrivano di nuove, chi le mostra le richiede.
    property int renderTick: 0
    Connections { target: Workspace; function onSnippetsReady() { root.renderTick++ } }
    readonly property string ink: ("" + Theme.text).slice(-6)

    // Un pezzo come nel PDF: intanto il testo; se non compila resta il testo.
    component Rendered: Item {
        id: piece
        property string text
        property string kind: "prosa"
        property string shown: text                    // il testo da mostrare intanto
        property bool mono: false
        property color textColor: Theme.text
        property int weight: Font.Normal
        property bool fit: true                        // sta nella larghezza che ha; altrimenti prende la sua
        readonly property string path: text !== "" && root.renderTick >= 0 ? Workspace.snippet(text, kind, root.ink) : ""
        readonly property bool ready: picture.status === Image.Ready
        Layout.fillWidth: true
        implicitWidth: ready ? picture.width : label.implicitWidth
        implicitHeight: ready ? picture.height : label.implicitHeight
        TxLabel {
            id: label
            visible: !piece.ready
            width: parent.width
            text: piece.shown
            color: piece.textColor
            font.weight: piece.weight
            font.family: piece.mono ? Theme.monoFamily : Qt.application.font.family
            wrapMode: piece.mono ? Text.WrapAnywhere : Text.WordWrap
            elide: Text.ElideNone
        }
        Image {
            id: picture
            visible: piece.ready
            source: piece.path !== "" ? "file://" + piece.path : ""
            asynchronous: true
            width: piece.fit ? Math.min(piece.width, sourceSize.width * 0.52) : sourceSize.width * 0.52
            height: sourceSize.width > 0 ? width * sourceSize.height / sourceSize.width : 0
            smooth: true
            mipmap: true
        }
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
            Layout.leftMargin: 2 * Theme.gap + 4      // staccata dal bordo
            Layout.rightMargin: Theme.gap
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.gap
            bottomMargin: 2 * Theme.gap
            model: root.items.length                   // le schede restano finché il numero delle righe non cambia
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                id: slot
                required property int index
                readonly property var modelData: root.items[index] || ({ type: "lesson" })
                readonly property string kind: modelData.type
                width: Math.min(list.width - 14, 1100)
                sourceComponent: kind === "lesson" ? lessonHead : kind === "doubt" ? doubtCard : kind === "issue" ? issueCard : declaredCard
                onLoaded: item.row = modelData
                onModelDataChanged: if (item) item.row = modelData
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
                visible: root.summary
                text: "Riapri tutta"
                flat: true
                hint: "Tutto ciò che era deciso in questa lezione torna da decidere"
                onClicked: Workspace.reopenLesson(row.lesson, root.section)
            }
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
            Rendered { text: card.row.what || ""; weight: Font.Medium }
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
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 6
                        TxLabel { text: "−"; color: Theme.bad; Layout.alignment: Qt.AlignTop }
                        Rendered { text: parent.modelData; kind: "latex"; mono: true; textColor: Theme.bad }
                    }
                }
                Repeater {
                    model: card.row.after || []
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 6
                        TxLabel { text: "+"; color: Theme.good; Layout.alignment: Qt.AlignTop }
                        Rendered { text: parent.modelData; kind: "latex"; mono: true; textColor: Theme.good }
                    }
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
                    // una lettura: com'è nel PDF; un clic la sceglie
                    Rectangle {
                        id: reading
                        required property var modelData
                        required property int index
                        readonly property bool mine: !card.row.open && card.row.chosen === index
                        width: Math.min(inside.implicitWidth + 2 * Theme.gap + 4, card.width - 2 * Theme.gap - 4)
                        height: Math.max(Theme.rowHeight + 2, inside.implicitHeight + Theme.gap)
                        radius: Theme.radius
                        color: readingArea.containsMouse ? Theme.hover : Theme.window
                        border.width: mine ? 2 : 1
                        border.color: mine ? Theme.accent : Theme.border
                        RowLayout {
                            id: inside
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - 2 * Theme.gap)
                            spacing: 6
                            TxLabel { visible: reading.mine; text: "✓"; color: Theme.accent }
                            Rendered {
                                Layout.fillWidth: false
                                fit: false
                                text: reading.modelData.latex || ""
                                kind: "latex"
                                shown: reading.modelData.text
                            }
                            TxLabel { visible: reading.index === 0 && card.row.open === true; text: "com'è scritto"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                        }
                        MouseArea { id: readingArea; anchors.fill: parent; hoverEnabled: true; onClicked: Workspace.chooseReading(card.row.lesson, card.row.id, reading.index) }
                        ToolTip.visible: readingArea.containsMouse
                        ToolTip.delay: 700
                        ToolTip.text: reading.modelData.text
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

    // un errore o una riga fuori misura dell'ultima compilazione
    Component {
        id: issueCard
        Card {
            id: card
            property var row: ({})
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TxLabel {
                    text: card.row.kind === "error" ? "Errore" : card.row.kind === "wide" ? "Riga troppo larga" : "Riga troppo vuota"
                    color: card.row.kind === "error" ? Theme.bad : Theme.warn
                    font.weight: Font.Medium
                }
                TxLabel { Layout.fillWidth: true; text: card.row.line > 0 ? "riga " + card.row.line : ""; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                TxButton { visible: card.row.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(card.row.lesson, card.row.line) }
                TxButton { text: "Sistema"; hint: "Sistema errori e righe di questa lezione"; enabled: !Workspace.busy; onClicked: Workspace.fixAll(card.row.lesson) }
            }
            TxLabel { Layout.fillWidth: true; visible: text !== ""; text: card.row.text || card.row.detail || ""; color: Theme.textMuted; wrapMode: Text.WordWrap; elide: Text.ElideNone }
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
            Rendered { text: card.row.text || ""; kind: "latex"; opacity: card.row.state === "tolta" ? 0.55 : 1 }
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
