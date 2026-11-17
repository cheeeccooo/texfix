import QtQuick
import QtQuick.Layouts
import Texfix

// Una scelta in una scheda da rivedere: un tasto largo con il suo colore. Il contenuto è il testo, o ciò che si mette
// dentro (una lettura resa come nel PDF).
Rectangle {
    id: choice
    property string text
    property color tone: Theme.surface
    property bool chosen: false
    default property alias content: inside.data
    signal clicked()
    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.rowHeight + 8, inside.childrenRect.height + 14)
    radius: Theme.radius
    opacity: enabled ? 1 : 0.35
    color: Qt.rgba(tone.r, tone.g, tone.b, area.pressed ? 0.45 : area.containsMouse ? 0.34 : chosen ? 0.4 : 0.22)
    border.width: chosen ? 2 : 1
    border.color: tone
    TxLabel {
        visible: choice.text !== ""
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.gap
        horizontalAlignment: Text.AlignHCenter
        text: (choice.chosen ? "✓ " : "") + choice.text
    }
    Item {
        id: inside
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Theme.gap
        height: childrenRect.height
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: choice.clicked() }
}
