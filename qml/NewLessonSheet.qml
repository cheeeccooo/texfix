import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Nuova lezione: numero e data proposti, le fonti. Una fonte si converte; più fonti le fonde l'agente, con un piano.
TxSheet {
    id: root
    title: "Nuova lezione"
    wantedWidth: 520
    property var sources: []                           // percorsi assoluti
    property var important: []                         // le fonti che decidono dove le altre dicono altro
    property string warning
    property bool warned: false                        // «Crea» ha già avvisato: un secondo clic tiene i valori scritti

    function reset() {
        const marks = Workspace.lessonMarks([])
        number.text = marks.next
        date.text = marks.today
        sources = []
        important = []
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
        const error = Workspace.createLesson(parseInt(number.text) || 0, date.text.trim(), sources, important)
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
        text: "Scansioni, PDF, immagini o documenti di questa lezione: trascinali qui, o aggiungili."
        color: Theme.textFaint
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    // le fonti in miniatura: un clic col tasto destro apre il menu (apri, dai importanza, togli)
    Flow {
        Layout.fillWidth: true
        spacing: Theme.gap
        visible: root.sources.length > 0
        Repeater {
            model: root.sources
            Rectangle {
                id: tile
                required property string modelData
                required property int index
                readonly property string name: modelData.substring(modelData.lastIndexOf("/") + 1)
                readonly property bool weighty: root.important.includes(modelData)
                readonly property string picture: /\.pdf$/i.test(modelData) ? "image://pdf/0/0/220/" + encodeURIComponent(modelData)
                                                  : /\.(png|jpe?g|gif|webp)$/i.test(modelData) ? "file://" + modelData : ""
                width: 96
                height: 124
                radius: Theme.radius
                color: Theme.surface
                border.width: weighty ? 2 : 1
                border.color: weighty ? Theme.accent : tileArea.containsMouse ? Theme.textFaint : Theme.border
                clip: true
                Image {
                    visible: tile.picture !== ""
                    x: 3; y: 3
                    width: parent.width - 6
                    height: parent.height - 30
                    source: tile.picture
                    sourceSize.width: 220
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    asynchronous: true
                }
                TxLabel {
                    visible: tile.picture === ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 34
                    text: tile.name.includes(".") ? tile.name.substring(tile.name.lastIndexOf(".") + 1).toUpperCase() : "FILE"
                    color: Theme.textFaint
                    font.weight: Font.DemiBold
                }
                TxLabel {
                    x: 4
                    width: parent.width - 8
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: (tile.weighty ? "★ " : "") + tile.name
                    font.pixelSize: Theme.smallSize
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                }
                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: { tileMenu.path = tile.modelData; tileMenu.index = tile.index; tileMenu.popup() }
                }
                ToolTip.visible: tileArea.containsMouse
                ToolTip.delay: 700
                ToolTip.text: tile.name + (tile.weighty ? " · importante" : "")
            }
        }
    }
    Menu {
        id: tileMenu
        property string path
        property int index
        MenuItem { text: "Apri"; onTriggered: Qt.openUrlExternally("file://" + tileMenu.path) }
        MenuItem {
            text: root.important.includes(tileMenu.path) ? "Togli importanza" : "Dai importanza"
            onTriggered: {
                const all = root.important.slice()
                const at = all.indexOf(tileMenu.path)
                if (at >= 0) all.splice(at, 1); else all.push(tileMenu.path)
                root.important = all
            }
        }
        MenuItem {
            text: "Togli"
            onTriggered: {
                const all = root.sources.slice()
                all.splice(tileMenu.index, 1)
                root.sources = all
                root.important = root.important.filter(p => all.includes(p))
            }
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
        text: "Più fonti: se non hanno parti in comune si uniscono e si convertono; se si sovrappongono le fonde l'agente, e prima ti mostra un piano da approvare. Una fonte importante (tasto destro) decide dove le altre dicono altro."
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
    DropArea {
        parent: root.background
        anchors.fill: parent
        onDropped: drop => { if (drop.hasUrls) root.add(Workspace.localPaths(drop.urls)) }
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
