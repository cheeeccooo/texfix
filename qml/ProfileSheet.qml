import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Il foglio dell'inizio, per un progetto nuovo e per una cartella che lo diventa. Per data o per argomento (e per
// argomento il programma del corso, se c'è); poi il profilo, come si scrive (uno di Texfix, uno dei miei, uno da creare
// con l'agente o da caricare); poi lo stile, allo stesso modo, con proposto quello che va con il profilo; se la cartella
// ha dei file, dove va ognuno: in arrivo (da smistare), da consultare, o dov'è. Tutto il resto si cambia dopo, dalle
// Impostazioni.
TxSheet {
    id: root
    title: root.files.length > 0 ? "La cartella diventa un progetto" : "Il progetto nuovo"
    wantedWidth: 620
    property string path
    // «data» o «argomento»: niente di scelto in partenza, il progetto si crea quando l'utente ha scelto
    property string organization: ""
    readonly property bool byTopic: organization === "argomento"
    property string program                            // per argomento: il programma del corso, facoltativo
    property string profile: "Neutro"                  // un nome, «@agente», «@file:<percorso>»
    property string style: "default"
    property var profiles: []
    property var styles: []
    // i file che la cartella ha già (Workspace.folderFiles), e dove va ognuno: file → «coda», «docs», «lascia»
    property var files: []
    property var placement: ({})
    property int revision: 0                           // cambia a ogni scelta: le righe rileggono, l'elenco resta dov'è
    signal chosen(string path, string profile, bool byTopic, string program, string style, var placement)
    function ask(path) {
        root.path = path
        organization = ""
        program = ""
        profiles = Workspace.profiles()
        styles = Workspace.styleLibrary()
        files = Workspace.folderFiles(path)
        const proposed = {}
        for (const f of files)
            proposed[f.file] = f.dove
        placement = proposed
        revision++
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
        value: root.organization
        onChosen: value => root.organization = value
    }
    TxLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        text: root.organization === "" ? "Per data: le lezioni di un corso, in ordine di calendario. Per argomento: un libro, una dispensa, un corso senza calendario, nell'ordine più logico. Si cambia anche dopo, dalle Impostazioni."
              : root.byTopic ? "Un libro, una dispensa, un corso senza calendario: il materiale va negli argomenti, nell'ordine più logico, e ogni pezzo ricorda da dove viene."
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

    // i file che la cartella ha già: una riga per cartella (sceglie per tutti i suoi file), poi i file, rientrati
    readonly property var rows: {
        const out = []
        const seen = {}
        for (const f of root.files) {
            const parts = f.file.split("/")
            for (let d = 1; d < parts.length; ++d) {
                const folder = parts.slice(0, d).join("/")
                if (!seen[folder]) {
                    seen[folder] = true
                    out.push({ folder: folder, name: parts[d - 1] + "/", depth: d - 1 })
                }
            }
            out.push({ file: f.file, name: parts[parts.length - 1], depth: parts.length - 1 })
        }
        return root.files.length > 1 ? [{ folder: "", name: "Tutti i file", depth: 0 }].concat(out) : out
    }
    function covers(row, file) { return row.file !== undefined ? file === row.file : row.folder === "" || file.startsWith(row.folder + "/") }
    function whereOf(row) {                            // di una cartella: la scelta comune, o niente se i suoi file sono divisi
        const inside = root.files.filter(f => root.covers(row, f.file)).map(f => root.placement[f.file])
        return inside.every(v => v === inside[0]) ? inside[0] : ""
    }
    function create() {
        if (root.organization === "")
            return
        root.close()
        root.chosen(root.path, root.profile, root.byTopic, root.program, root.style, root.placement)
    }
    function place(row, value) {
        const next = Object.assign({}, root.placement)
        for (const f of root.files)
            if (root.covers(row, f.file))
                next[f.file] = value
        root.placement = next
        root.revision++
    }
    readonly property var counts: {
        root.revision
        const out = { coda: 0, docs: 0, lascia: 0 }
        for (const f of root.files)
            out[root.placement[f.file]]++
        return out
    }
    TxLabel { visible: root.files.length > 0; Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "I file che ci sono già"; font.weight: Font.Medium }
    TxLabel {
        visible: root.files.length > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        text: (root.files.length === 1 ? "Un file" : root.files.length + " file") + ": " + root.counts.coda + " in arrivo, " + root.counts.docs + " da consultare, "
              + root.counts.lascia + " dove sono. In arrivo è il materiale da smistare: non parte niente finché non premi «Smista». "
              + "Da consultare resta nel progetto com'è, con le sue cartelle."
    }
    Rectangle {
        visible: root.files.length > 0
        Layout.fillWidth: true
        implicitHeight: Math.min(fileList.contentHeight, 8 * (Theme.rowHeight + 6)) + 2
        radius: Theme.radius
        color: Theme.window
        border.width: 1
        border.color: Theme.border
        ListView {
            id: fileList
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.rows
            delegate: Item {
                required property var modelData
                width: fileList.width
                height: Theme.rowHeight + 6
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.gap + 14 * modelData.depth
                    anchors.rightMargin: Theme.gap
                    spacing: Theme.gap
                    TxLabel {
                        Layout.fillWidth: true
                        text: modelData.name
                        elide: Text.ElideMiddle
                        color: modelData.file === undefined ? Theme.textMuted : Theme.text
                        font.weight: modelData.file === undefined ? Font.Medium : Font.Normal
                    }
                    TxSegmented {
                        choices: [{ value: "coda", label: "In arrivo" }, { value: "docs", label: "Da consultare" }, { value: "lascia", label: "Dov'è" }]
                        value: { root.revision; return root.whereOf(modelData) }
                        onChosen: value => root.place(modelData, value)
                    }
                }
            }
        }
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
        TxButton {
            text: "Crea il progetto"
            primary: true
            enabled: root.organization !== ""
            hint: enabled ? "" : "Scegli prima come si organizza: per data o per argomento"
            onClicked: root.create()
        }
    ]
}
