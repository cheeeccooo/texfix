import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Nuova lezione: numero e data proposti, le fonti. Una fonte si converte; più fonti le fonde l'agente, con un piano.
TxSheet {
    id: root
    title: "Nuova lezione"
    wantedWidth: 520
    property var sources: []                           // percorsi assoluti
    property string warning
    property bool warned: false                        // «Crea» ha già avvisato: un secondo clic tiene i valori scritti

    function reset() {
        const marks = Workspace.lessonMarks([])
        number.text = marks.next
        date.text = marks.today
        sources = []
        warning = ""
        warned = false
    }
    onAboutToShow: reset()
    function add(paths) {
        const all = sources.slice()
        for (const p of paths)
            if (!all.includes(p))
                all.push(p)
        sources = all
        warned = false
    }
    function fromSources() {                           // «Prendi dal file»: senza modello, dai nomi e dal testo
        const marks = Workspace.lessonMarks(sources)
        if (marks.number > 0)
            number.text = marks.number
        if (marks.date !== "")
            date.text = marks.date
        warning = marks.conflict !== "" ? marks.conflict
                : marks.number === 0 && marks.date === "" ? "Nelle fonti non trovo né il numero né la data: scrivili tu."
                : marks.number === 0 ? "Nelle fonti non trovo il numero: scrivilo tu." : marks.date === "" ? "Nelle fonti non trovo la data: scrivila tu." : ""
    }
    function create() {
        const marks = Workspace.lessonMarks(sources)
        const differs = (marks.number > 0 && marks.number !== parseInt(number.text)) || (marks.date !== "" && marks.date !== date.text.trim())
        if (differs && !warned) {
            warning = "Le fonti dicono " + (marks.number > 0 ? "lezione " + marks.number : "") + (marks.date !== "" ? " del " + marks.date : "")
                    + ". Un altro clic su «Crea» tiene i valori scritti."
            warned = true
            return
        }
        const error = Workspace.createLesson(parseInt(number.text) || 0, date.text.trim(), sources)
        if (error !== "") {
            warning = error
            return
        }
        close()
    }

    component Field: Rectangle {
        property alias text: input.text
        implicitHeight: Theme.rowHeight + 4
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
            onTextEdited: root.warned = false
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel { text: "Numero" }
        Field { id: number; Layout.preferredWidth: 70 }
        TxLabel { text: "Data"; Layout.leftMargin: Theme.gap }
        Field { id: date; Layout.fillWidth: true }
        TxButton { text: "Prendi dal file"; enabled: root.sources.length > 0; hint: "Cerca numero e data nei nomi e nel testo delle fonti"; onClicked: root.fromSources() }
    }
    TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "Fonti"; font.weight: Font.Medium }
    TxLabel {
        Layout.fillWidth: true
        visible: root.sources.length === 0
        text: "Scansioni, PDF, immagini o documenti di questa lezione."
        color: Theme.textFaint
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Repeater {
        model: root.sources
        RowLayout {
            id: row
            required property string modelData
            required property int index
            Layout.fillWidth: true
            spacing: Theme.gap
            TxLabel { Layout.fillWidth: true; text: row.modelData.substring(row.modelData.lastIndexOf("/") + 1); elide: Text.ElideMiddle }
            TxButton { text: "Togli"; flat: true; onClicked: { const all = root.sources.slice(); all.splice(row.index, 1); root.sources = all } }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxButton { text: "Aggiungi file…"; onClicked: picker.open() }
        TxButton {
            visible: Workspace.queue.length > 0
            text: "Da quelle da convertire (" + Workspace.queue.length + ")"
            onClicked: root.add(Workspace.queue.map(q => Workspace.root + "/" + q.file))
        }
    }
    TxLabel {
        Layout.fillWidth: true
        visible: root.sources.length > 1
        text: "Più fonti: le fonde l'agente, e prima ti mostra un piano da approvare."
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    TxLabel {
        Layout.fillWidth: true
        visible: root.warning !== ""
        text: root.warning
        color: Theme.warn
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    FileDialog {
        id: picker
        title: "Le fonti della lezione"
        fileMode: FileDialog.OpenFiles
        onAccepted: root.add(Workspace.localPaths(selectedFiles))
    }

    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton { text: "Crea"; primary: true; enabled: root.sources.length > 0 && !Workspace.busy; onClicked: root.create() }
    ]
}
