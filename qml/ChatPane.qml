import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// La chat con l'agente: i messaggi mentre arrivano, gli strumenti che usa, il modello per chat, gli allegati, lo storico.
Rectangle {
    id: root
    color: Theme.panel
    signal closeAsked()
    signal shotAsked(string what)                      // una foto di un pezzo della finestra: editor | pdf | finestra
    property var attachments: []                       // i file da mandare con il prossimo messaggio (percorsi)

    function sendNow() {
        if ((input.text.trim() === "" && attachments.length === 0) || Chat.running)
            return
        Chat.send(input.text, attachments)
        input.text = ""
        attachments = []
    }
    function attach(paths) {
        const all = attachments.slice()
        for (const p of paths)
            if (!all.includes(p))
                all.push(p)
        attachments = all
    }
    // ⌘L: un pezzo di un file entra nel messaggio, con il suo posto.
    function quote(file, from, to, text) {
        const where = from === to ? "riga " + from : "righe " + from + "-" + to
        input.text = (input.text.trim() === "" ? "" : input.text.replace(/\s+$/, "") + "\n\n") + "In " + file + ", " + where + ":\n```\n" + text.replace(/\s+$/, "") + "\n```\n"
        input.focusEnd()
    }
    function showHistory() { history.chats = Chat.chats(); history.armed = ""; history.open() }
    function isImage(path) { return /\.(png|jpe?g|gif|webp)$/i.test(path) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gap + 4
            Layout.rightMargin: 6
            implicitHeight: Theme.rowHeight + 8
            spacing: Theme.gap
            // il nome della chat; un clic apre lo storico
            TxButton {
                Layout.fillWidth: true
                flat: true
                text: (Chat.title !== "" ? Chat.title : "Agente") + "  ▾"
                hint: "Le chat di questo progetto"
                onClicked: root.showHistory()
            }
            TxSegmented {
                implicitHeight: Theme.rowHeight - 4
                implicitWidth: 130
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
                readonly property bool tool: modelData.role === "tool"
                readonly property bool small: tool || modelData.role === "note"
                readonly property bool ask: modelData.role === "ask"
                readonly property var files: modelData.attachments || []
                property bool expanded: false          // una riga di strumento aperta: che cosa ha chiesto
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
                        text: row.tool ? (row.modelData.detail ? (row.expanded ? "▾ " : "▸ ") : "⚙ ") + row.modelData.text + (row.expanded ? "\n" + row.modelData.detail : "")
                              : row.ask ? "L'agente chiede di usare: " + row.modelData.text
                              : row.modelData.text + (row.files.length > 0 ? (row.modelData.text !== "" ? "\n" : "") + row.files.map(f => "📎 " + f.substring(f.lastIndexOf("/") + 1)).join("\n") : "")
                        textFormat: row.modelData.role === "assistant" ? Text.MarkdownText : Text.PlainText
                        wrapMode: Text.Wrap
                        color: row.modelData.role === "note" ? Theme.warn : row.small ? Theme.textFaint : Theme.text
                        font.pixelSize: row.small ? Theme.smallSize : Theme.textSize
                        font.family: row.expanded ? Theme.monoFamily : Qt.application.font.family
                        lineHeight: 1.1
                    }
                    MouseArea { anchors.fill: parent; enabled: row.tool && !!row.modelData.detail; onClicked: row.expanded = !row.expanded }
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
            // gli allegati del prossimo messaggio: le immagini in miniatura, il resto con il nome; un clic li toglie
            Flow {
                Layout.fillWidth: true
                visible: root.attachments.length > 0
                spacing: 6
                Repeater {
                    model: root.attachments
                    Rectangle {
                        id: chip
                        required property string modelData
                        required property int index
                        readonly property bool image: root.isImage(modelData)
                        width: image ? 56 : Math.min(180, name.implicitWidth + 30)
                        height: image ? 56 : Theme.rowHeight
                        radius: Theme.radius
                        color: Theme.surface
                        border.width: 1
                        border.color: chipArea.containsMouse ? Theme.accent : Theme.border
                        clip: true
                        Image {
                            visible: chip.image
                            anchors.fill: parent
                            anchors.margins: 1
                            source: chip.image ? "file://" + chip.modelData : ""
                            sourceSize.width: 112
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        TxLabel {
                            id: name
                            visible: !chip.image
                            anchors.verticalCenter: parent.verticalCenter
                            x: 8
                            width: parent.width - 16
                            text: chip.modelData.substring(chip.modelData.lastIndexOf("/") + 1)
                            font.pixelSize: Theme.smallSize
                        }
                        Rectangle {
                            visible: chipArea.containsMouse
                            anchors.right: parent.right; anchors.top: parent.top
                            width: 16; height: 16; radius: 8
                            color: Theme.bad
                            Text { anchors.centerIn: parent; text: "×"; color: Theme.onAccent; font.pixelSize: 11 }
                        }
                        MouseArea {
                            id: chipArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { const all = root.attachments.slice(); all.splice(chip.index, 1); root.attachments = all }
                        }
                        ToolTip.visible: chipArea.containsMouse
                        ToolTip.delay: 600
                        ToolTip.text: chip.modelData + " · un clic lo toglie"
                    }
                }
            }
            TxTextArea {
                id: input
                Layout.fillWidth: true
                Layout.maximumHeight: 160
                placeholder: "Scrivi all'agente…  (⌘↩ invia, ⌘L porta qui la selezione)"
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        root.sendNow()
                        event.accepted = true
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                TxButton {
                    text: (Chat.mode === "chiedi" ? "Chiedi conferma" : Chat.mode === "autonomo" ? "Autonomo" : "Automatico") + "  ▾"
                    flat: true
                    enabled: !Chat.running
                    hint: "Quanto l'agente fa da solo"
                    onClicked: modeMenu.popup()
                }
                TxButton { text: "＋"; flat: true; implicitWidth: 26; hint: "Allega dei file"; onClicked: attachDialog.open() }
                TxButton { text: "Foto"; flat: true; hint: "Allega una foto di un pezzo della finestra"; onClicked: shotMenu.popup() }
                TxLabel { Layout.fillWidth: true; text: Chat.waiting ? "Aspetta la tua conferma" : Chat.running ? "L'agente sta lavorando…" : ""; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                TxButton { visible: Chat.running; text: "Ferma"; onClicked: Chat.stop() }
                TxButton { visible: !Chat.running; text: "Invia"; primary: true; enabled: !Workspace.busy; onClicked: root.sendNow() }
            }
        }
    }
    // i file trascinati sulla chat si allegano
    DropArea {
        anchors.fill: parent
        onDropped: drop => { if (drop.hasUrls) root.attach(Workspace.localPaths(drop.urls)) }
    }
    FileDialog {
        id: attachDialog
        title: "Allega alla chat"
        fileMode: FileDialog.OpenFiles
        onAccepted: root.attach(Workspace.localPaths(selectedFiles))
    }
    Menu {
        id: shotMenu
        MenuItem { text: "L'editor"; onTriggered: root.shotAsked("editor") }
        MenuItem { text: "Il PDF"; onTriggered: root.shotAsked("pdf") }
        MenuItem { text: "Tutta la finestra"; onTriggered: root.shotAsked("finestra") }
    }
    Menu {
        id: modeMenu
        MenuItem { text: "Automatico: fa da solo ciò che è sicuro, chiede per il resto"; onTriggered: Chat.mode = "auto" }
        MenuItem { text: "Chiedi conferma: prima di ogni modifica"; onTriggered: Chat.mode = "chiedi" }
        MenuItem { text: "Autonomo: non chiede mai"; onTriggered: Chat.mode = "autonomo" }
    }
    // lo storico: le chat del progetto, la più recente in cima; elimina in due clic
    Popup {
        id: history
        property var chats: []
        property string armed                          // la chat su cui «Elimina» è già stato premuto una volta
        x: Theme.gap
        y: Theme.rowHeight + 10
        width: root.width - 2 * Theme.gap
        height: Math.min(root.height - 80, Math.max(60, historyList.contentHeight + 2 * Theme.gap))
        padding: Theme.gap
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.width: 1; border.color: Theme.border }
        TxLabel { anchors.centerIn: parent; visible: history.chats.length === 0; text: "Ancora nessuna chat."; color: Theme.textFaint }
        ListView {
            id: historyList
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: history.chats
            delegate: Rectangle {
                id: entry
                required property var modelData
                width: historyList.width
                height: Theme.rowHeight + 4
                radius: Theme.radius
                color: modelData.current ? Theme.selected : entryArea.containsMouse ? Theme.hover : "transparent"
                MouseArea {
                    id: entryArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !Chat.running
                    onClicked: { Chat.openChat(entry.modelData.id); history.close() }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.gap
                    anchors.rightMargin: 4
                    spacing: Theme.gap
                    TxLabel { Layout.fillWidth: true; text: entry.modelData.title }
                    TxLabel { text: entry.modelData.when; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                    TxButton {
                        flat: history.armed !== entry.modelData.id
                        text: history.armed === entry.modelData.id ? "Elimina davvero" : "Elimina"
                        implicitHeight: Theme.rowHeight - 2
                        enabled: !Chat.running
                        onClicked: {
                            if (history.armed !== entry.modelData.id)
                                return history.armed = entry.modelData.id
                            Chat.removeChat(entry.modelData.id)
                            history.armed = ""
                            history.chats = Chat.chats()
                        }
                    }
                }
            }
        }
    }
    Rectangle { width: 1; height: parent.height; color: Theme.border }
}
