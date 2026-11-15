import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Il pannello Progetto: le lezioni, con lo stato a colpo d'occhio, e gli altri file del documento.
Rectangle {
    id: root
    property string currentFile
    signal fileChosen(string file)
    color: Theme.panel

    component GroupTitle: TxLabel {
        width: parent ? parent.width - 2 * Theme.gap : 0
        x: Theme.gap + 4
        height: Theme.rowHeight + 4
        verticalAlignment: Text.AlignBottom
        bottomPadding: 4
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        font.capitalization: Font.AllUppercase
    }

    // Una riga: il nome a sinistra, a destra piccolo il dettaglio; il nome intero nel suggerimento.
    component TreeRow: Rectangle {
        id: row
        property string file
        property string name
        property string detail
        property color dot: "transparent"
        property string number
        property bool done: false
        property bool lesson: false
        width: parent ? parent.width : 0
        height: Theme.rowHeight
        color: root.currentFile === file ? Theme.selected : area.containsMouse ? Theme.hover : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.gap + 4
            anchors.rightMargin: Theme.gap + 4
            spacing: 6
            Rectangle {
                visible: row.dot.a > 0
                implicitWidth: row.done ? 12 : 8
                implicitHeight: implicitWidth
                radius: implicitWidth / 2
                color: row.dot
                Text { anchors.centerIn: parent; visible: row.done; text: "✓"; color: Theme.onAccent; font.pixelSize: 9; font.weight: Font.Bold }
            }
            TxLabel { visible: row.number !== ""; text: row.number; color: Theme.textMuted; font.features: { "tnum": 1 } }
            TxLabel { Layout.fillWidth: true; text: row.name }
            TxLabel { text: row.detail; color: Theme.textFaint; font.pixelSize: Theme.smallSize; Layout.maximumWidth: row.width * 0.4 }
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.LeftButton)
                    return root.fileChosen(row.file)
                if (row.file.includes(":"))
                    return                             // un lavoro, non un file
                fileMenu.file = row.file
                fileMenu.lesson = row.lesson
                fileMenu.done = row.done
                fileMenu.popup()
            }
        }
        ToolTip.visible: area.containsMouse
        ToolTip.delay: 800
        ToolTip.text: (row.number !== "" ? row.number + " · " : "") + row.name + (row.detail !== "" ? " · " + row.detail : "")
    }

    // Un gruppo di file con il suo titolo: un clic sul titolo lo apre o lo chiude, e resta così.
    component FileGroup: Column {
        id: group
        property string title
        property var files: []
        property bool open: true
        width: parent ? parent.width : 0
        GroupTitle {
            text: (group.open ? "▾ " : "▸ ") + group.title + (group.open ? "" : "  " + group.files.length)
            MouseArea { anchors.fill: parent; onClicked: group.open = !group.open }
        }
        Repeater {
            model: group.open ? group.files : []
            TreeRow {
                required property var modelData
                file: modelData.file
                name: modelData.name
            }
        }
    }

    // il menu di una lezione
    signal compileAsked(string file)
    signal renameAsked(string file)
    Menu {
        id: fileMenu
        property string file
        property bool lesson
        property bool done
        readonly property bool text: file.endsWith(".tex") || file.endsWith(".sty")
        MenuItem { text: "Apri"; onTriggered: root.fileChosen(fileMenu.file) }
        MenuItem { text: "Apri e compila"; visible: fileMenu.text; height: visible ? implicitHeight : 0; onTriggered: root.compileAsked(fileMenu.file) }
        MenuItem { text: "Apri con l'app predefinita"; onTriggered: Workspace.openExternally(fileMenu.file) }
        MenuItem { text: "Mostra nel Finder"; onTriggered: Workspace.revealFile(fileMenu.file) }
        MenuSeparator {}
        MenuItem { text: "Copia il percorso"; onTriggered: Workspace.copyText(Workspace.root + "/" + fileMenu.file) }
        MenuItem { text: "Copia il nome"; onTriggered: Workspace.copyText(fileMenu.file.substring(fileMenu.file.lastIndexOf("/") + 1)) }
        MenuSeparator {}
        MenuItem { text: "Rinomina…"; onTriggered: root.renameAsked(fileMenu.file) }
        MenuItem { text: "Duplica"; onTriggered: Workspace.duplicateFile(fileMenu.file) }
        MenuItem { text: "Sposta nel Cestino"; onTriggered: Workspace.trashFile(fileMenu.file) }
        MenuSeparator { visible: fileMenu.lesson; height: visible ? implicitHeight : 0 }
        MenuItem { text: "Aggiungi fonti a questa lezione…"; visible: fileMenu.lesson; height: visible ? implicitHeight : 0; onTriggered: root.sourcesAsked(fileMenu.file) }
        MenuItem { text: fileMenu.done ? "Non è più fatta" : "Segna come fatta"; visible: fileMenu.lesson; height: visible ? implicitHeight : 0; onTriggered: Workspace.setDone(fileMenu.file, !fileMenu.done) }
    }

    function dotColor(state) {
        return state === "good" ? Theme.good : state === "warn" ? Theme.warn : state === "bad" ? Theme.bad : Theme.idle
    }

    signal newLessonAsked()
    signal materialAsked()
    signal sourcesAsked(string lesson)

    // i due ingressi, sopra l'albero
    Row {
        id: entries
        x: Theme.gap
        y: Theme.gap
        spacing: 6
        TxButton { text: "＋ Nuova lezione"; width: Math.min(implicitWidth, (root.width - 2 * Theme.gap - 6) / 2); onClicked: root.newLessonAsked() }
        TxButton { text: "＋ Aggiungi materiale"; width: Math.min(implicitWidth, (root.width - 2 * Theme.gap - 6) / 2); hint: "Porta dei file fra le fonti da convertire"; onClicked: root.materialAsked() }
    }
    // i file trascinati sull'albero entrano fra le fonti da convertire
    DropArea {
        anchors.fill: parent
        onDropped: drop => { if (drop.hasUrls) Workspace.sortMaterial(Workspace.addSources(Workspace.localPaths(drop.urls))) }
    }

    TxScroll {
        anchors.fill: parent
        anchors.topMargin: entries.height + 2 * Theme.gap
        contentHeight: column.implicitHeight + Theme.gap
        Column {
            id: column
            width: root.width

            // i lavori aperti: un piano da approvare, in corso, o con l'esito da leggere
            GroupTitle { text: "Lavori"; visible: Workspace.jobs.length > 0 }
            Repeater {
                model: Workspace.jobs
                TreeRow {
                    required property var modelData
                    file: "piano:" + modelData.id
                    name: modelData.title
                    detail: modelData.state === "piano" ? "da approvare" : modelData.state === "esito" ? "esito" : "in corso"
                    dot: modelData.state === "piano" ? Theme.warn : modelData.state === "esito" ? Theme.good : Theme.accent
                }
            }
            GroupTitle { text: Workspace.byTopic ? "Argomenti" : "Lezioni" }
            TxLabel {
                visible: Workspace.lessons.length === 0
                x: Theme.gap + 4
                width: parent.width - 2 * Theme.gap - 8
                height: implicitHeight + Theme.gap
                text: "Ancora nessuna lezione."
                color: Theme.textFaint
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Repeater {
                model: Workspace.lessons
                TreeRow {
                    required property var modelData
                    file: modelData.file
                    name: modelData.title
                    number: modelData.number > 0 && !Workspace.byTopic ? modelData.number : ""
                    detail: modelData.detail !== "" ? modelData.detail : Workspace.byTopic ? "" : modelData.date
                    dot: modelData.done ? Theme.good : root.dotColor(modelData.state)
                    done: modelData.done
                    lesson: true
                }
            }
            FileGroup { title: "Da convertire"; files: Workspace.queue; visible: files.length > 0 }
            FileGroup { title: "Documento"; files: Workspace.documentFiles }
            FileGroup { title: "Da consultare"; files: Workspace.documents; visible: files.length > 0 }
            // gli esiti dei piani eseguiti: il titolo a sinistra, quando a destra; si aprono impaginati
            GroupTitle { text: "Esiti"; visible: Workspace.outcomes.length > 0 }
            Repeater {
                model: Workspace.outcomes
                TreeRow {
                    required property var modelData
                    file: modelData.file
                    name: modelData.name
                    detail: modelData.detail
                }
            }
            FileGroup { title: "Immagini"; files: Workspace.images; open: false; visible: files.length > 0 }
        }
    }
}
