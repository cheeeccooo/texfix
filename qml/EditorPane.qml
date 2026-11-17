import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// L'editor: una scheda per file aperto, ognuna con il suo testo e la sua pila di annullamenti.
Rectangle {
    id: root
    color: Theme.surface
    property bool wrap: true                           // ⌥Z: a capo o no, per tutti gli editor
    readonly property string currentFile: tabs.count > 0 && current >= 0 && current < tabs.count ? tabs.get(current).file : ""
    property int current: -1
    readonly property int currentLine: current >= 0 && editors.itemAt(current) ? editors.itemAt(current).currentLine : 0
    signal jumpAsked(string file, int line)
    signal chatAsked()
    signal tabClosed(string file)
    signal manualAsked(string lesson, string block, int line, string what)
    signal layoutRefreshAsked()

    // Che cosa si apre in una scheda: text, pdf, image; il resto (vuoto) lo apre un altro programma.
    function kindOf(file) {
        if (file.startsWith("salvataggi/esiti/") || file.startsWith("rivedi:") || file.startsWith("piano:") || file.startsWith("diff:") || file.startsWith("commit:"))
            return "review"
        const name = file.substring(file.lastIndexOf("/") + 1).toLowerCase()
        const ext = name.includes(".") ? name.substring(name.lastIndexOf(".") + 1) : ""
        if (ext === "pdf")
            return "pdf"
        if (["png", "jpg", "jpeg", "gif", "webp", "svg"].includes(ext))
            return "image"
        if (["tex", "sty", "cls", "bib", "md", "txt", "json", "latexmkrc", ""].includes(ext))
            return "text"
        return ""
    }

    ListModel { id: tabs }                             // file, modified

    function indexOf(file) {
        for (let i = 0; i < tabs.count; ++i)
            if (tabs.get(i).file === file)
                return i
        return -1
    }
    // Apre il file in una scheda (o va alla sua) e, se c'è, porta alla riga.
    function openFile(file, line) {
        if (kindOf(file) === "")
            return Workspace.openExternally(file)
        let at = indexOf(file)
        if (at < 0) {
            tabs.append({ file: file, modified: false })
            at = tabs.count - 1
        }
        current = at
        const editor = editors.itemAt(at)
        if (editor && line > 0)
            editor.goToLine(line)
        if (editor)
            editor.focusText()
    }
    function closeTab(at) {
        const editor = editors.itemAt(at)
        if (editor && editor.modified)
            editor.save()                              // ogni cosa è salvata subito: chiudere non perde niente
        const closed = tabs.get(at).file
        tabs.remove(at)
        current = Math.min(current >= at ? current - 1 : current, tabs.count - 1)
        if (current < 0 && tabs.count > 0)
            current = 0
        tabClosed(closed)
    }
    // La selezione dell'editor in vista, per portarla nella chat (⌘L): file, text, from, to; null se non è un testo.
    function selection() {
        const page = current >= 0 ? editors.itemAt(current) : null
        if (!page || page.kind !== "text" || !page.item)
            return null
        const s = page.item.selection()
        s.file = page.file
        return s
    }
    function showShots() {                              // per le prove: apre le miniature della prima mossa
        const page = current >= 0 ? editors.itemAt(current) : null
        if (page && page.item && page.item.showShots)
            page.item.showShots()
    }
    function insertLine(text) {
        const page = current >= 0 ? editors.itemAt(current) : null
        if (page && page.kind === "text" && page.item)
            page.item.insertLine(text)
    }
    // Porta il pannello aperto su una lezione.
    function showLesson(lesson) {
        const page = current >= 0 ? editors.itemAt(current) : null
        if (page && page.item && page.item.showLesson)
            Qt.callLater(() => page.item.showLesson(lesson))
    }
    function closeAll() { tabs.clear(); current = -1 }
    // Un file rinominato o buttato: la sua scheda lo segue o si chiude (prima si salva ciò che c'era).
    function fileRenamed(from, to) {
        const at = indexOf(from)
        if (at < 0)
            return
        tabs.remove(at)
        current = Math.min(current, tabs.count - 1)
        if (to !== "")
            openFile(to, 0)
    }
    function saveAll() {
        for (let i = 0; i < tabs.count; ++i) {
            const editor = editors.itemAt(i)
            if (editor && editor.modified)
                editor.save()
        }
    }
    function toggleWrap() { wrap = !wrap }
    // ⌘Z resta del testo finché la sua pila ne ha: true se l'ha fatto l'editor.
    function textUndo() {
        const page = current >= 0 ? editors.itemAt(current) : null
        if (!page || page.kind !== "text" || !page.item || !page.item.canUndo)
            return false
        page.item.undo()
        return true
    }
    function textRedo() {
        const page = current >= 0 ? editors.itemAt(current) : null
        if (!page || page.kind !== "text" || !page.item || !page.item.canRedo)
            return false
        page.item.redo()
        return true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // le schede
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight + 6
            color: Theme.window
            visible: tabs.count > 0
            ListView {
                id: tabBar
                anchors.fill: parent
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                model: tabs
                currentIndex: root.current
                delegate: Rectangle {
                    id: tab
                    required property int index
                    required property string file
                    required property bool modified
                    readonly property bool active: index === root.current
                    width: Math.min(220, tabName.implicitWidth + 46)
                    height: tabBar.height
                    color: active ? Theme.surface : tabArea.containsMouse ? Theme.hover : "transparent"
                    TxLabel {
                        id: tabName
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.gap + 2
                        anchors.right: close.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: tab.file.startsWith("diff:") ? "± " + tab.file.substring(tab.file.lastIndexOf("/") + 1).replace("diff:", "") : tab.file.startsWith("commit:") ? "Commit " + tab.file.substring(7) : tab.file.startsWith("piano:") ? "Piano" : tab.file.startsWith("rivedi:") ? "Rivedi " + tab.file.substring(7) : tab.file.substring(tab.file.lastIndexOf("/") + 1)
                        color: tab.active ? Theme.text : Theme.textMuted
                    }
                    MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; onClicked: root.current = tab.index }
                    // la × (un pallino se ci sono modifiche non salvate), un po' staccata dal bordo
                    Rectangle {
                        id: close
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18; radius: 4
                        color: closeArea.containsMouse ? Theme.border : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: tab.modified && !closeArea.containsMouse ? "●" : "×"
                            color: Theme.textMuted
                            font.pixelSize: tab.modified && !closeArea.containsMouse ? 9 : 15
                        }
                        MouseArea { id: closeArea; anchors.fill: parent; hoverEnabled: true; onClicked: root.closeTab(tab.index) }
                    }
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                    ToolTip.visible: tabArea.containsMouse
                    ToolTip.delay: 800
                    ToolTip.text: tab.file
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border; z: -1 }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.current
            Repeater {
                id: editors
                model: tabs
                // una scheda: un testo da scrivere, un PDF o un'immagine da guardare
                Loader {
                    id: page
                    required property int index
                    required property string file
                    readonly property string kind: root.kindOf(file)
                    readonly property bool modified: kind === "text" && item ? item.modified : false
                    onModifiedChanged: tabs.setProperty(index, "modified", modified)
                    function save() { if (kind === "text" && item) item.save() }
                    function goToLine(line) {              // per un PDF la «riga» è la pagina
                        if (kind === "text" && item)
                            item.goToLine(line)
                        else if (kind === "pdf" && item)
                            Qt.callLater(() => item.show(line, 0, 0))
                    }
                    function focusText() { if (kind === "text" && item) item.focusText() }
                    readonly property int currentLine: kind === "text" && item ? item.currentLine : 0

                    Component.onCompleted: {
                        if (file.startsWith("salvataggi/esiti/"))
                            setSource("MarkdownView.qml", { file: file })
                        else if (file.startsWith("diff:") || file.startsWith("commit:"))
                            setSource("DiffView.qml", { what: file })
                        else if (file.startsWith("piano:"))
                            setSource("PlanView.qml", { planId: file.substring(6) })
                        else if (file === "rivedi:impaginazione")
                            setSource("LayoutView.qml", {})
                        else if (file === "rivedi:figure")
                            setSource("FiguresView.qml", {})
                        else if (kind === "review")
                            setSource("ReviewView.qml", { section: file.substring(7) })
                        else if (kind === "pdf")
                            setSource("PdfPane.qml", { source: Workspace.root + "/" + file })
                        else if (kind === "image")
                            setSource("ImageView.qml", { file: file })
                        else
                            setSource("TexEditor.qml", { file: file, wrap: Qt.binding(() => root.wrap) })
                    }
                    Connections {
                        target: page.kind === "review" ? page.item : null
                        ignoreUnknownSignals: true
                        function onCloseAsked() { root.closeTab(page.index) }
                        function onChatAsked() { root.chatAsked() }
                        function onManualAsked(lesson, block, line, what) { root.manualAsked(lesson, block, line, what) }
                        function onRefreshAsked() { root.layoutRefreshAsked() }
                        function onLineAsked(file, line) { root.openFile(file, line) }
                    }
                    Connections {
                        target: page.kind === "text" ? page.item : null
                        function onWrapToggled() { root.toggleWrap() }
                        function onJumpAsked(line) { root.jumpAsked(page.file, line) }
                    }
                }
            }
        }
    }

    TxLabel {
        anchors.centerIn: parent
        visible: tabs.count === 0
        text: "Apri una lezione dal pannello Progetto."
        color: Theme.textFaint
    }
}
