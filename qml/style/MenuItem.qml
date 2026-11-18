import QtQuick
import QtQuick.Templates as T
import Texfix

// Lo stile di Texfix: una voce di menu, con la riga sotto il puntatore nel colore di Texfix e gli angoli tondi.
T.MenuItem {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    topPadding: 5
    bottomPadding: 5
    leftPadding: 10
    rightPadding: 10
    spacing: 8
    readonly property color ink: !enabled ? Theme.textFaint : highlighted ? Theme.onAccent : Theme.text
    indicator: Text {
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        visible: control.checkable
        text: control.checked ? "✓" : ""
        color: control.ink
        font.pixelSize: Theme.textSize
        width: 14
    }
    arrow: Text {
        x: control.width - width - control.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        visible: control.subMenu
        text: "›"
        color: control.ink
        font.pixelSize: Theme.textSize + 3
    }
    contentItem: Text {
        leftPadding: control.checkable ? 14 + control.spacing : 0
        rightPadding: control.subMenu ? 12 + control.spacing : 0
        text: control.text
        color: control.ink
        font.pixelSize: Theme.textSize
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 26
        radius: 6
        color: control.highlighted ? Theme.accent : "transparent"
        opacity: control.down ? 0.85 : 1
    }
}
