import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Un progetto nuovo. Per data o per argomento (e per argomento il programma del corso, se c'è); poi il profilo, come si
// scrive (uno di Texfix, uno dei miei, uno da creare con l'agente o da caricare); poi lo stile, allo stesso modo, con
// proposto quello che va con il profilo. Tutto si cambia dopo, dalle Impostazioni.
TxSheet {
    id: root
    title: "Il progetto nuovo"
    wantedWidth: 620
    property string path
    property bool byTopic: false
    property string program                            // per argomento: il programma del corso, facoltativo
    property string profile: "Neutro"                  // un nome, «@agente», «@file:<percorso>»
    property string style: "default"
    property var profiles: []
    property var styles: []
    signal chosen(string path, string profile, bool byTopic, string program, string style)
    function ask(path) {
        root.path = path
        byTopic = false
        program = ""
        profiles = Workspace.profiles()
        styles = Workspace.styleLibrary()
        pickProfile("Neutro")
        open()
    }
    // lo stile che va con un profilo di Texfix
    function pickProfile(name) {
        profile = name
        const paired = { "Neutro": "default", "Scientifico": "science_default", "Umanistico": "prose_default" }
        if (paired[name] !== undefined)
            style = paired[name]
    }
    function label(value) { return value === "@agente" ? "con l'agente" : value.startsWith("@file:") ? value.split("/").pop() : value }

    component Choice: TxButton {
        property string value
        property string current
        signal picked(string value)
        primary: value === current
        onClicked: picked(value)
    }

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
        text: root.byTopic ? "Un libro, una dispensa, un corso senza calendario: il materiale va negli argomenti, nell'ordine più logico, e ogni pezzo ricorda da dove viene."
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

    TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "Il profilo: come si scrive"; font.weight: Font.Medium }
    Flow {
        Layout.fillWidth: true
        spacing: Theme.gap
        Repeater {
            model: root.profiles
            Choice { required property string modelData; text: modelData; value: modelData; current: root.profile; onPicked: v => root.pickProfile(v) }
        }
        Choice { text: "Crea con l'agente"; value: "@agente"; current: root.profile; onPicked: v => root.profile = v }
        Choice { text: root.profile.startsWith("@file:") ? root.label(root.profile) : "Carica da file…"; value: root.profile.startsWith("@file:") ? root.profile : "@carica"
                 current: root.profile; onPicked: profileDialog.open() }
    }
    TxLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        text: root.profile === "@agente" ? "L'agente ti fa qualche domanda sul corso e propone un profilo; lo vedi e decidi. Resta nella libreria per altri progetti."
              : root.profile === "Neutro" ? "Semplice, per ogni materia." : root.profile === "Scientifico" ? "Per le materie con formule: conti, notazione, grafici."
              : root.profile === "Umanistico" ? "Per le materie di testo: tesi e argomenti, citazioni, autori." : ""
    }

    TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "Lo stile: come appare"; font.weight: Font.Medium }
    Flow {
        Layout.fillWidth: true
        spacing: Theme.gap
        Repeater {
            model: root.styles
            Choice { required property string modelData; text: modelData; value: modelData; current: root.style; onPicked: v => root.style = v }
        }
        Choice { text: "Crea con l'agente"; value: "@agente"; current: root.style; onPicked: v => root.style = v }
        Choice { text: root.style.startsWith("@file:") ? root.label(root.style) : "Carica .sty…"; value: root.style.startsWith("@file:") ? root.style : "@carica"
                 current: root.style; onPicked: styleDialog.open() }
    }

    FileDialog {
        id: programDialog
        title: "Il programma, l'indice o la scaletta del corso"
        onAccepted: root.program = Workspace.localPath(selectedFile)
    }
    FileDialog {
        id: profileDialog
        title: "Un profilo di Texfix (.profilo.md)"
        nameFilters: ["Profili di Texfix (*.profilo.md *.md)"]
        onAccepted: root.profile = "@file:" + Workspace.localPath(selectedFile)
    }
    FileDialog {
        id: styleDialog
        title: "Uno stile LaTeX (.sty)"
        nameFilters: ["Stili LaTeX (*.sty)"]
        onAccepted: root.style = "@file:" + Workspace.localPath(selectedFile)
    }
    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton { text: "Crea il progetto"; primary: true; onClicked: { root.close(); root.chosen(root.path, root.profile, root.byTopic, root.program, root.style) } }
    ]
}
