import QtQuick
import Texfix

// Una scelta fra poche voci, tutte in vista: `choices` è un elenco di { value, label }.
Rectangle {
    id: root
    property var choices: []
    property string value
    signal chosen(string value)
    implicitWidth: row.implicitWidth + 2
    implicitHeight: Theme.rowHeight
    radius: Theme.radius
    color: Theme.window
    border.width: 1
    border.color: Theme.border
    Row {
        id: row
        anchors.centerIn: parent
        Repeater {
            model: root.choices
            Rectangle {
                id: segment
                required property var modelData
                readonly property bool on: modelData.value === root.value
                width: Math.max(label.implicitWidth + 2 * Theme.gap + 4, (root.width - 2) / Math.max(1, root.choices.length))
                height: root.height - 2
                radius: Theme.radius - 1
                color: on ? Theme.accent : area.containsMouse ? Theme.hover : "transparent"
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: segment.modelData.label
                    color: segment.on ? Theme.onAccent : Theme.text
                    font.pixelSize: Theme.textSize
                }
                MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.chosen(segment.modelData.value) }
            }
        }
    }
}
