import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Version control: le modifiche da salvare, il commit (con conferma), la cronologia. Un clic su un file o su un commit
// ne apre le differenze in una scheda.
Rectangle {
    id: root
    color: Theme.panel
    signal opened(string what)                         // «diff:<file>» o «commit:<hash>»
    property var changes: []
    property var history: []
    property bool confirm: false
    property string error

    function reload() { changes = Workspace.gitChanges(); history = Workspace.gitLog() }
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onTreeChanged() { if (root.visible) root.reload() } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap
        visible: !Workspace.hasRepository
        TxLabel { Layout.fillWidth: true; text: "Questo progetto non ha ancora un repository."; color: Theme.textMuted; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        TxButton { text: "Inizializza"; onClicked: Workspace.gitInit() }
        Item { Layout.fillHeight: true }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: Workspace.hasRepository
        TxTextArea {
            id: message
            Layout.fillWidth: true
            Layout.margins: Theme.gap
            Layout.maximumHeight: 110
            placeholder: "Che cosa hai cambiato?"
            onEdited: { root.confirm = false; root.error = "" }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gap
            Layout.rightMargin: Theme.gap
            TxLabel { Layout.fillWidth: true; text: root.error; color: Theme.warn; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap; elide: Text.ElideNone }
            TxButton {
                text: root.confirm ? "Sì, fai il commit" : "Commit"
                primary: root.confirm
                enabled: root.changes.length > 0 && message.text.trim() !== ""
                onClicked: {
                    if (!root.confirm)
                        return root.confirm = true
                    root.confirm = false
                    root.error = Workspace.gitCommit(message.text)
                    if (root.error === "")
                        message.text = ""
                }
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.gap
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: [{ head: root.changes.length === 0 ? "Niente da salvare" : "Modifiche (" + root.changes.length + ")" }]
                   .concat(root.changes).concat([{ head: "Cronologia" }]).concat(root.history)
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool head: modelData.head !== undefined
                width: list.width
                height: head ? Theme.rowHeight + 4 : Theme.rowHeight
                color: !head && area.containsMouse ? Theme.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.gap + 4
                    anchors.rightMargin: Theme.gap + 4
                    spacing: 6
                    TxLabel {
                        Layout.fillWidth: true
                        text: row.head ? row.modelData.head : row.modelData.file !== undefined ? row.modelData.file : row.modelData.subject
                        color: row.head ? Theme.textMuted : Theme.text
                        font.pixelSize: row.head ? Theme.smallSize : Theme.textSize
                        font.capitalization: row.head ? Font.AllUppercase : Font.MixedCase
                        verticalAlignment: row.head ? Text.AlignBottom : Text.AlignVCenter
                        elide: row.modelData.file !== undefined ? Text.ElideLeft : Text.ElideRight
                    }
                    TxLabel {
                        visible: !row.head
                        text: row.modelData.state !== undefined ? row.modelData.state : (row.modelData.when || "")
                        color: row.modelData.state === "nuovo" ? Theme.good : row.modelData.state === "tolto" ? Theme.bad : Theme.textFaint
                        font.pixelSize: Theme.smallSize
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !row.head
                    onClicked: root.opened(row.modelData.file !== undefined ? "diff:" + row.modelData.file : "commit:" + row.modelData.hash)
                }
            }
        }
    }
}
