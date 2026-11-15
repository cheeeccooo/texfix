import QtQuick
import QtQuick.Controls
import Texfix

// Le differenze di un file o di un commit: le righe tolte in rosso, quelle aggiunte in verde.
Rectangle {
    id: root
    required property string what
    color: Theme.surface
    property var lines: []
    function reload() { lines = Workspace.gitDiff(what).split("\n") }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.lines
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            required property string modelData
            readonly property bool added: modelData.startsWith("+") && !modelData.startsWith("+++")
            readonly property bool removed: modelData.startsWith("-") && !modelData.startsWith("---")
            readonly property bool meta: modelData.startsWith("@@") || modelData.startsWith("diff ") || modelData.startsWith("index ")
                                         || modelData.startsWith("+++") || modelData.startsWith("---")
            width: list.width
            height: text.implicitHeight + 2
            color: added ? Qt.rgba(Theme.good.r, Theme.good.g, Theme.good.b, 0.16) : removed ? Qt.rgba(Theme.bad.r, Theme.bad.g, Theme.bad.b, 0.16) : "transparent"
            Text {
                id: text
                x: Theme.gap
                width: parent.width - 2 * Theme.gap
                text: modelData
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                font.family: Theme.monoFamily
                font.pixelSize: Theme.monoSize - 1
                color: meta ? Theme.textFaint : Theme.text
            }
        }
    }
    TxLabel { anchors.centerIn: parent; visible: root.lines.length <= 1; text: "Nessuna differenza."; color: Theme.textFaint }
}
