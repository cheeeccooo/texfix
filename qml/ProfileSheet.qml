import QtQuick
import QtQuick.Layouts
import Texfix

// Un progetto nuovo: da quale profilo di partenza. Si vede solo se ce n'è almeno uno.
TxSheet {
    id: root
    title: "Da dove parte il progetto?"
    wantedWidth: 460
    property string path
    property var names: []
    signal chosen(string path, string profile)
    function ask(path) { root.path = path; names = Workspace.profiles(); open() }

    TxButton {
        Layout.fillWidth: true
        text: "Nessun profilo: un classico documento matematico"
        onClicked: { root.close(); root.chosen(root.path, "") }
    }
    Repeater {
        model: root.names
        TxButton {
            required property string modelData
            Layout.fillWidth: true
            text: modelData
            primary: true
            onClicked: { root.close(); root.chosen(root.path, modelData) }
        }
    }
    buttons: TxButton { text: "Annulla"; onClicked: root.close() }
}
