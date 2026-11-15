import QtQuick
import Texfix

// Un documento in markdown (un esito archiviato), impaginato e da leggere.
Rectangle {
    id: root
    required property string file
    color: Theme.surface
    TxScroll {
        anchors.fill: parent
        contentHeight: text.implicitHeight + 4 * Theme.gap
        Text {
            id: text
            x: 2 * Theme.gap
            y: 2 * Theme.gap
            width: Math.min(root.width - 4 * Theme.gap, 820)
            text: Workspace.read(root.file)
            textFormat: Text.MarkdownText
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: Theme.textSize
            lineHeight: 1.15
        }
    }
}
