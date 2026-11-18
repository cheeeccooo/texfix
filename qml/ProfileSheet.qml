import QtQuick
import QtQuick.Layouts
import Texfix

// Un progetto nuovo: per data o per argomento (si cambia quando si vuole dalle impostazioni), e da quale profilo di
// partenza, se ce n'è.
TxSheet {
    id: root
    title: "Il progetto nuovo"
    wantedWidth: 480
    property string path
    property var names: []
    property bool byTopic: false
    signal chosen(string path, string profile, bool byTopic)
    function ask(path) { root.path = path; names = Workspace.profiles(); byTopic = false; open() }

    TxLabel { Layout.fillWidth: true; text: "Come si organizza"; font.weight: Font.Medium }
    TxSegmented {
        Layout.fillWidth: true
        choices: [{ value: "data", label: "Per data" }, { value: "argomento", label: "Per argomento" }]
        value: root.byTopic ? "argomento" : "data"
        onChosen: value => root.byTopic = value === "argomento"
    }
    TxLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        text: root.byTopic ? "Un libro, una dispensa, un corso senza calendario: il materiale va negli argomenti, e ogni pezzo ricorda da dove viene."
                           : "Le lezioni di un corso, in ordine di calendario. Ciò che non ha data diventa una lezione senza data, in fondo."
    }
    TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "Da dove parte"; font.weight: Font.Medium }
    TxButton {
        Layout.fillWidth: true
        text: "Nessun profilo: un classico documento matematico"
        primary: root.names.length === 0
        onClicked: { root.close(); root.chosen(root.path, "", root.byTopic) }
    }
    Repeater {
        model: root.names
        RowLayout {
            id: row
            required property string modelData
            property bool sure: false
            Layout.fillWidth: true
            spacing: Theme.gap
            TxButton { Layout.fillWidth: true; text: row.modelData; primary: true; onClicked: { root.close(); root.chosen(root.path, row.modelData, root.byTopic) } }
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
