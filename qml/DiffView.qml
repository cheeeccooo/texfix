import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Le differenze di un file o di un commit. Affiancate: a sinistra com'era, a destra com'è, le righe tolte in rosso e
// quelle aggiunte in verde. Con poco spazio, o a richiesta, in colonna.
Rectangle {
    id: root
    required property string what
    color: Theme.surface
    property var rows: []                              // kind (meta | same | change), left, right
    property bool wanted: true                         // affiancate, se c'è spazio
    readonly property bool sideBySide: wanted && width >= 640

    // Dal testo di `git diff` alle righe affiancate: in ogni tratto le righe tolte stanno accanto a quelle aggiunte.
    function parse(text) {
        const out = []
        let minus = [], plus = []
        function flush() {
            const n = Math.max(minus.length, plus.length)
            for (let i = 0; i < n; ++i)
                out.push({ kind: "change", left: i < minus.length ? minus[i] : null, right: i < plus.length ? plus[i] : null })
            minus = []
            plus = []
        }
        for (const line of text.split("\n")) {
            if (line.startsWith("diff ") || line.startsWith("index ") || line.startsWith("+++") || line.startsWith("---") || line.startsWith("@@")
                    || line.startsWith("new file") || line.startsWith("deleted file") || line.startsWith("\\")) {
                flush()
                if (line.startsWith("@@") || line.startsWith("diff "))
                    out.push({ kind: "meta", left: line.startsWith("diff ") ? line.substring(line.lastIndexOf(" b/") + 3) : line, right: null })
            } else if (line.startsWith("-")) {
                minus.push(line.substring(1))
            } else if (line.startsWith("+")) {
                plus.push(line.substring(1))
            } else {
                flush()
                if (line !== "" || out.length > 0)
                    out.push({ kind: line.startsWith(" ") ? "same" : "meta", left: line.startsWith(" ") ? line.substring(1) : line, right: line.startsWith(" ") ? line.substring(1) : null })
            }
        }
        flush()
        return out
    }
    function reload() { rows = parse(Workspace.gitDiff(what)) }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()

    component Cell: Rectangle {
        property var line                              // null: da questa parte non c'è niente
        property color tint
        property bool changed: false
        implicitHeight: label.implicitHeight + 2
        color: line === null ? Theme.window : changed ? Qt.rgba(tint.r, tint.g, tint.b, 0.16) : "transparent"
        Text {
            id: label
            x: Theme.gap
            width: parent.width - 2 * Theme.gap
            text: parent.line === null ? "" : parent.line
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            font.family: Theme.monoFamily
            font.pixelSize: Theme.monoSize - 1
            color: Theme.text
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap
            TxLabel { Layout.fillWidth: true; text: root.what.startsWith("commit:") ? "Commit " + root.what.substring(7) : root.what.substring(5); color: Theme.textMuted }
            TxButton { text: root.wanted ? "In colonna" : "Affiancate"; flat: true; onClicked: root.wanted = !root.wanted }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.rows
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool meta: modelData.kind === "meta"
                readonly property bool change: modelData.kind === "change"
                width: list.width
                height: meta ? head.implicitHeight + 8 : root.sideBySide ? Math.max(left.implicitHeight, right.implicitHeight) : stack.implicitHeight
                TxLabel {
                    id: head
                    visible: row.meta
                    x: Theme.gap
                    y: 4
                    width: parent.width - 2 * Theme.gap
                    text: row.modelData.left || ""
                    color: Theme.textFaint
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.smallSize
                }
                // affiancate
                Cell { id: left; visible: !row.meta && root.sideBySide; width: row.width / 2; height: row.height; line: row.modelData.left; tint: Theme.bad; changed: row.change }
                Cell { id: right; visible: !row.meta && root.sideBySide; x: row.width / 2; width: row.width / 2; height: row.height; line: row.modelData.right; tint: Theme.good; changed: row.change }
                Rectangle { visible: !row.meta && root.sideBySide; x: row.width / 2; width: 1; height: row.height; color: Theme.border }
                // in colonna
                Column {
                    id: stack
                    visible: !row.meta && !root.sideBySide
                    width: row.width
                    Cell { visible: row.modelData.left !== null && (row.change || true); width: row.width; line: row.modelData.left; tint: Theme.bad; changed: row.change; height: visible ? implicitHeight : 0 }
                    Cell { visible: row.change && row.modelData.right !== null; width: row.width; line: row.modelData.right; tint: Theme.good; changed: true; height: visible ? implicitHeight : 0 }
                }
            }
        }
    }
    TxLabel { anchors.centerIn: parent; visible: root.rows.length === 0; text: "Nessuna differenza."; color: Theme.textFaint }
}
