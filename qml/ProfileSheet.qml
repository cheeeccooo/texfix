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
        RowLayout {
            id: row
            required property string modelData
            property bool sure: false
            Layout.fillWidth: true
            spacing: Theme.gap
            TxButton { Layout.fillWidth: true; text: row.modelData; primary: true; onClicked: { root.close(); root.chosen(root.path, row.modelData) } }
            TxButton {
                text: row.sure ? "Sì, elimina" : "Elimina"
                flat: !row.sure
                onClicked: {
                    if (!row.sure)
                        return row.sure = true
                    Workspace.removeProfile(row.modelData)
                    root.names = Workspace.profiles()
                }
            }
        }
    }
    buttons: TxButton { text: "Annulla"; onClicked: root.close() }
}
