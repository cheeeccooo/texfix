import QtQuick
import QtQuick.Dialogs
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
    property string program                            // per argomento: il programma del corso, facoltativo
    signal chosen(string path, string profile, bool byTopic, string program)
    function ask(path) { root.path = path; names = Workspace.profiles(); byTopic = false; program = ""; open() }

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
                           : "Le lezioni di un corso, in ordine di calendario. Ciò che non ha data va dove lo mette la logica del corso."
    }
    RowLayout {                                        // per argomento: il programma del corso, se c'è
        visible: root.byTopic
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel {
            Layout.fillWidth: true
            text: root.program === "" ? "Hai il programma, l'indice o la scaletta del corso? Gli argomenti seguiranno il suo ordine."
                                      : "Programma del corso: " + root.program.split("/").pop()
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        TxButton { text: root.program === "" ? "Scegli…" : "Cambia…"; onClicked: programDialog.open() }
    }
    FileDialog {
        id: programDialog
        title: "Il programma, l'indice o la scaletta del corso"
        onAccepted: root.program = Workspace.localPath(selectedFile)
    }
    TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "Da dove parte"; font.weight: Font.Medium }
    TxButton {
        Layout.fillWidth: true
        text: "Nessun profilo: un classico documento matematico"
        primary: root.names.length === 0
        onClicked: { root.close(); root.chosen(root.path, "", root.byTopic, root.program) }
    }
    Repeater {
        model: root.names
        RowLayout {
            id: row
            required property string modelData
            property bool sure: false
            Layout.fillWidth: true
            spacing: Theme.gap
            TxButton { Layout.fillWidth: true; text: row.modelData; primary: true; onClicked: { root.close(); root.chosen(root.path, row.modelData, root.byTopic, root.program) } }
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
