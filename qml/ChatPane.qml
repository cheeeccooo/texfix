import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// La chat con l'agente: i messaggi mentre arrivano, gli strumenti che usa, il modello per chat.
Rectangle {
    id: root
    color: Theme.panel
    signal closeAsked()

    function sendNow() {
        if (input.text.trim() === "" || Chat.running)
            return
        Chat.send(input.text)
        input.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gap + 4
            Layout.rightMargin: 6
            implicitHeight: Theme.rowHeight + 8
            spacing: Theme.gap
            TxLabel { Layout.fillWidth: true; text: "Agente"; color: Theme.textMuted; font.pixelSize: Theme.smallSize; font.capitalization: Font.AllUppercase }
            TxSegmented {
                implicitHeight: Theme.rowHeight - 4
                implicitWidth: 150
                choices: [{ value: "sonnet", label: "Sonnet" }, { value: "opus", label: "Opus" }]
                value: Chat.model
                onChosen: value => Chat.model = value
            }
            TxButton { text: "Nuova"; flat: true; hint: "Comincia una chat nuova"; enabled: !Chat.running; onClicked: Chat.newChat() }
            TxButton { text: "×"; flat: true; implicitWidth: 24; hint: "Chiudi"; onClicked: root.closeAsked() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.gap
            topMargin: Theme.gap
            bottomMargin: Theme.gap
            model: Chat.messages
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: Qt.callLater(positionViewAtEnd)
            onContentHeightChanged: if (Chat.running) Qt.callLater(positionViewAtEnd)
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool mine: modelData.role === "user"
                readonly property bool small: modelData.role === "tool" || modelData.role === "note"
                readonly property bool ask: modelData.role === "ask"
                width: list.width
                height: bubble.height + (ask ? buttons.height + Theme.gap : 0)
                Row {
                    id: buttons
                    visible: row.ask
                    x: Theme.gap + 4
                    anchors.top: bubble.bottom
                    anchors.topMargin: Theme.gap / 2
                    spacing: Theme.gap
                    TxButton { text: "Consenti"; primary: true; onClicked: Chat.answer(true) }
                    TxButton { text: "Nega"; onClicked: Chat.answer(false) }
                }
                Rectangle {
                    id: bubble
                    x: row.mine ? row.width - width - Theme.gap - 4 : Theme.gap + 4
                    width: Math.min(row.width - 2 * Theme.gap - 8 - (row.mine ? 24 : 0), Math.max(40, label.implicitWidth + 2 * Theme.gap))
                    height: label.height + (row.small ? 4 : 2 * Theme.gap - 2)
                    radius: Theme.radius
                    color: row.mine ? Theme.selected : row.small ? "transparent" : Theme.surface
                    border.width: row.mine || row.small ? 0 : 1
                    border.color: Theme.border
                    Text {
                        id: label
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.gap
                        width: bubble.width - 2 * Theme.gap
                        text: row.modelData.role === "tool" ? "⚙ " + row.modelData.text : row.ask ? "L'agente chiede di usare: " + row.modelData.text : row.modelData.text
                        textFormat: row.modelData.role === "assistant" ? Text.MarkdownText : Text.PlainText
                        wrapMode: Text.Wrap
                        color: row.modelData.role === "note" ? Theme.warn : row.small ? Theme.textFaint : Theme.text
                        font.pixelSize: row.small ? Theme.smallSize : Theme.textSize
                        lineHeight: 1.1
                    }
                }
            }
        }
        TxLabel {
            Layout.fillWidth: true
            Layout.margins: 2 * Theme.gap
            visible: Chat.messages.length === 0
            text: "Chiedi all'agente di cambiare, controllare o spiegare qualcosa nel progetto. Vede solo questo progetto e lavora con gli strumenti di Texfix."
            color: Theme.textFaint
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap
            spacing: Theme.gap
            TxTextArea {
                id: input
                Layout.fillWidth: true
                Layout.maximumHeight: 160
                placeholder: "Scrivi all'agente…  (⌘↩ invia)"
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        root.sendNow()
                        event.accepted = true
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TxButton {
                    text: (Chat.mode === "chiedi" ? "Chiedi conferma" : Chat.mode === "autonomo" ? "Autonomo" : "Automatico") + "  ▾"
                    flat: true
                    enabled: !Chat.running
                    hint: "Quanto l'agente fa da solo"
                    onClicked: modeMenu.popup()
                }
                TxLabel { Layout.fillWidth: true; text: Chat.waiting ? "Aspetta la tua conferma" : Chat.running ? "L'agente sta lavorando…" : ""; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                TxButton { visible: Chat.running; text: "Ferma"; onClicked: Chat.stop() }
                TxButton { visible: !Chat.running; text: "Invia"; primary: true; enabled: !Workspace.busy; onClicked: root.sendNow() }
            }
        }
    }
    Menu {
        id: modeMenu
        MenuItem { text: "Automatico: fa da solo ciò che è sicuro, chiede per il resto"; onTriggered: Chat.mode = "auto" }
        MenuItem { text: "Chiedi conferma: prima di ogni modifica"; onTriggered: Chat.mode = "chiedi" }
        MenuItem { text: "Autonomo: non chiede mai"; onTriggered: Chat.mode = "autonomo" }
    }
    Rectangle { width: 1; height: parent.height; color: Theme.border }
}
