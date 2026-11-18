import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import Texfix

// Lo stile di Texfix: un menu è un foglio chiaro, arrotondato, con un'ombra leggera, largo quanto la voce più lunga.
T.Menu {
    id: control
    // la voce più lunga, fra un minimo e un limite di lettura
    readonly property real widest: {
        let w = 0
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item) w = Math.max(w, item.implicitWidth)
        }
        return w
    }
    implicitWidth: Math.min(Math.max(180, widest + leftPadding + rightPadding), 480)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    margins: 6
    padding: 5
    overlap: 4
    delegate: MenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
    }
    background: Rectangle {
        implicitWidth: 180
        implicitHeight: 36
        radius: 10
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        layer.enabled: true
        layer.effect: MultiEffect { shadowEnabled: true; shadowBlur: 0.8; shadowOpacity: 0.25; shadowVerticalOffset: 4; shadowColor: "black"; autoPaddingEnabled: true }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 110; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 } }
    T.Overlay.modal: Rectangle { color: "#33000000" }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
