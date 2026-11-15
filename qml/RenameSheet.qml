import QtQuick
import QtQuick.Layouts
import Texfix

// Rinomina un file. Se è una lezione, il documento la segue.
TxSheet {
    id: root
    title: "Rinomina"
    wantedWidth: 420
    property string file
    property string error
    function ask(file) {
        root.file = file
        error = ""
        input.text = file.substring(file.lastIndexOf("/") + 1)
        open()
        input.forceActiveFocus()
        input.select(0, input.text.lastIndexOf(".") > 0 ? input.text.lastIndexOf(".") : input.text.length)
    }
    function apply() {
        error = Workspace.renameFile(file, input.text)
        if (error === "")
            close()
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.rowHeight + 6
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: input.activeFocus ? Theme.accent : Theme.border
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: Theme.gap
            anchors.rightMargin: Theme.gap
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text
            font.pixelSize: Theme.textSize
            selectByMouse: true
            clip: true
            onAccepted: root.apply()
        }
    }
    TxLabel { Layout.fillWidth: true; visible: root.error !== ""; text: root.error; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }
    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton { text: "Rinomina"; primary: true; onClicked: root.apply() }
    ]
}
