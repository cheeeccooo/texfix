import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import Texfix

// Lo stile di Texfix: un suggerimento è un cartellino chiaro, arrotondato, con un'ombra leggera; il testo va a capo.
T.ToolTip {
    id: control
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: -height - 6
    width: Math.min(implicitWidth, 360)                // oltre, va a capo
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    margins: 8
    topPadding: 6
    bottomPadding: 7
    leftPadding: 10
    rightPadding: 10
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent
    contentItem: Text {
        text: control.text
        color: Theme.text
        font.pixelSize: Theme.smallSize + 1
        lineHeight: 1.15
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        radius: 8
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        layer.enabled: true
        layer.effect: MultiEffect { shadowEnabled: true; shadowBlur: 0.6; shadowOpacity: 0.22; shadowVerticalOffset: 3; shadowColor: "black"; autoPaddingEnabled: true }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
}
