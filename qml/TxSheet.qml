import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Una finestra dentro la finestra: non supera mai la finestra, il contenuto in mezzo scorre, i tasti in fondo
// restano sempre visibili.
Popup {
    id: sheet
    property string title
    property int wantedWidth: 440
    property int wantedHeight: 0                       // 0: alta quanto il contenuto
    default property alias content: body.data
    property alias buttons: buttonRow.data
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(wantedWidth, (Overlay.overlay ? Overlay.overlay.width : 480) - 4 * Theme.gap)
    height: Math.min(wantedHeight > 0 ? wantedHeight : implicitHeight, (Overlay.overlay ? Overlay.overlay.height : 380) - 4 * Theme.gap)
    padding: 2 * Theme.gap
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.surface; radius: Theme.radius + 2; border.width: 1; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.5 : 0.25) }
    contentItem: ColumnLayout {
        spacing: 2 * Theme.gap
        TxLabel {
            Layout.fillWidth: true
            visible: sheet.title !== ""
            text: sheet.title
            font.pixelSize: Theme.textSize + 3
            font.weight: Font.DemiBold
        }
        TxScroll {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: body.implicitHeight
            contentHeight: body.implicitHeight
            ColumnLayout {
                id: body
                width: scroll.width
                spacing: Theme.gap
            }
        }
        RowLayout {
            id: buttonRow
            Layout.alignment: Qt.AlignRight
            spacing: Theme.gap
        }
    }
}
