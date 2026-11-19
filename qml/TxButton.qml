import QtQuick
import QtQuick.Controls
import Texfix

// Un tasto. Spento si vede sbiadito e non risponde; `hint` è il suggerimento (con la scorciatoia).
Rectangle {
    id: root
    property string text
    property string hint
    property bool primary: false
    property bool flat: false
    signal clicked()

    implicitWidth: label.implicitWidth + 2 * Theme.gap + 4
    implicitHeight: Theme.rowHeight
    radius: Theme.radius
    opacity: enabled ? 1 : 0.4
    color: primary ? (area.pressed ? Qt.darker(Theme.accent, 1.15) : Theme.accent)
                   : area.pressed ? Theme.selected : area.containsMouse ? Theme.hover : flat ? "transparent" : Theme.surface
    border.width: primary || flat ? 0 : 1
    border.color: Theme.border

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, root.width - Theme.gap)
        text: root.text
        color: root.primary ? Theme.onAccent : Theme.text
        font.pixelSize: Theme.textSize
        elide: Text.ElideRight
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
    ToolTip.visible: hint !== "" && area.containsMouse
    ToolTip.delay: 600
    ToolTip.text: Theme.keys(hint)
}
