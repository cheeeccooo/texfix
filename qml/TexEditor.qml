import QtQuick
import QtQuick.Controls
import Texfix

// L'editor di un file di testo: numeri di riga, evidenziazione, la riga del cursore.
Item {
    id: page
    required property string file
    property bool wrap: true
    readonly property bool modified: edit.textDocument.modified
    readonly property int currentLine: backend.lineOf(edit.cursorPosition)
    readonly property bool canUndo: edit.canUndo
    readonly property bool canRedo: edit.canRedo
    function undo() { edit.undo() }
    function redo() { edit.redo() }
    signal wrapToggled()
    signal jumpAsked(int line)                         // ⌘-clic su una riga: al PDF

    function load() {
        edit.text = Workspace.read(file)
        edit.textDocument.modified = false
        Workspace.watchFile(file)
    }
    function save() {
        if (Workspace.save(file, edit.text))
            edit.textDocument.modified = false
    }
    function goToLine(line) {
        edit.cursorPosition = backend.positionOf(line)
        const rect = edit.cursorRectangle
        flick.contentY = Math.max(0, Math.min(rect.y - flick.height / 3, flick.contentHeight - flick.height))
    }
    function focusText() { edit.forceActiveFocus() }
    Component.onCompleted: load()

    // Il file è cambiato su disco (un lavoro, un altro programma): senza modifiche in sospeso si rilegge.
    Connections {
        target: Workspace
        function onFilesChanged() {
            if (page.modified)
                return
            const text = Workspace.read(page.file)
            if (text === edit.text)
                return
            const line = page.currentLine
            edit.text = text
            edit.textDocument.modified = false
            edit.cursorPosition = backend.positionOf(line)
        }
    }

    EditorBackend { id: backend; document: edit.textDocument }

    LineGutter {
        id: gutter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: neededWidth
        document: edit.textDocument
        contentY: flick.contentY
        topPadding: edit.topPadding
        currentLine: page.currentLine
        // sui numeri di riga si scorre, e un clic porta alla riga
        MouseArea {
            anchors.fill: parent
            onClicked: mouse => { edit.cursorPosition = backend.positionOf(gutter.lineAt(mouse.y)); edit.forceActiveFocus() }
            onWheel: wheel => {
                const dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 2
                flick.contentY = Math.max(0, Math.min(flick.contentY - dy, Math.max(0, flick.contentHeight - flick.height)))
            }
        }
    }
    Flickable {
        id: flick
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: page.wrap ? Flickable.VerticalFlick : Flickable.HorizontalAndVerticalFlick
        contentWidth: page.wrap ? width : Math.max(width, edit.implicitWidth)
        contentHeight: Math.max(height, edit.implicitHeight)
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar { policy: page.wrap ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded }

        function reveal(rect) {                        // il cursore resta in vista
            if (rect.y < contentY)
                contentY = rect.y
            else if (rect.y + rect.height > contentY + height)
                contentY = rect.y + rect.height - height
            if (rect.x < contentX)
                contentX = Math.max(0, rect.x - 40)
            else if (rect.x + rect.width > contentX + width)
                contentX = rect.x + rect.width - width + 40
        }

        Rectangle {                                    // la riga del cursore
            y: edit.cursorRectangle.y
            width: flick.contentWidth
            height: edit.cursorRectangle.height
            color: Theme.currentLine
        }
        TextEdit {
            id: edit
            width: flick.contentWidth
            height: flick.contentHeight
            topPadding: 6
            bottomPadding: 6
            leftPadding: 4
            rightPadding: 12
            font.family: Theme.monoFamily
            font.pixelSize: Theme.monoSize
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.text
            textFormat: TextEdit.PlainText
            wrapMode: page.wrap ? TextEdit.Wrap : TextEdit.NoWrap
            selectByMouse: true
            persistentSelection: true
            tabStopDistance: 4 * fontMetrics.averageCharacterWidth
            onCursorRectangleChanged: flick.reveal(cursorRectangle)
            // ⌥ non scrive caratteri speciali (⌥Z è «a capo o no»); le frecce con ⌥ passano
            Keys.onPressed: event => {
                if ((event.modifiers & Qt.AltModifier) && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) && event.text !== "") {
                    if (event.key === Qt.Key_Z)
                        page.wrapToggled()
                    event.accepted = true
                }
            }
            FontMetrics { id: fontMetrics; font: edit.font }
            // ⌘-clic su una riga porta al PDF
            TapHandler {
                acceptedModifiers: Qt.ControlModifier
                onTapped: point => page.jumpAsked(backend.lineOf(edit.positionAt(point.position.x, point.position.y)))
            }
        }
    }
    Rectangle { anchors.left: gutter.right; width: 1; height: parent.height; color: Theme.border; opacity: 0.6 }
}
