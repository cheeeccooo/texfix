import QtQuick
import QtQuick.Templates as T
import Texfix

// Lo stile di Texfix: una linea sottile fra due gruppi di voci.
T.MenuSeparator {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    contentItem: Rectangle { implicitWidth: 160; implicitHeight: 1; color: Theme.border }
}
