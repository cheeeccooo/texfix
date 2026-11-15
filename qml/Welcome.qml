import QtQuick
import QtQuick.Layouts
import Texfix

// La pagina di benvenuto: un progetto nuovo, una cartella da aprire, i progetti recenti.
Item {
    id: root
    signal newAsked()
    signal openAsked()
    signal projectChosen(string path)

    TxScroll {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 6 * Theme.gap

        ColumnLayout {
            id: column
            width: Math.min(520, root.width - 4 * Theme.gap)
            x: (root.width - width) / 2
            y: 3 * Theme.gap + Math.max(0, (root.height - implicitHeight - 6 * Theme.gap) / 3)
            spacing: Theme.gap

            TxLabel {
                Layout.fillWidth: true
                text: "Texfix"
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
            TxLabel {
                Layout.fillWidth: true
                text: "Dagli appunti di ogni tipo ad appunti LaTeX da studiare."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            RowLayout {
                Layout.topMargin: Theme.gap
                spacing: Theme.gap
                TxButton { text: "Nuovo progetto…"; primary: true; hint: "⌘N"; onClicked: root.newAsked() }
                TxButton { text: "Apri cartella…"; hint: "⌘O"; onClicked: root.openAsked() }
            }
            TxLabel {
                Layout.fillWidth: true
                Layout.topMargin: 2 * Theme.gap
                visible: Workspace.recents.length > 0
                text: "Progetti recenti"
                color: Theme.textMuted
                font.pixelSize: Theme.smallSize
                font.capitalization: Font.AllUppercase
            }
            Repeater {
                model: Workspace.recents
                Rectangle {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 2 * Theme.rowHeight - 6
                    radius: Theme.radius
                    color: area.containsMouse ? Theme.hover : Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gap + 4
                        anchors.rightMargin: Theme.gap + 4
                        spacing: 0
                        Item { Layout.fillHeight: true }
                        TxLabel { Layout.fillWidth: true; text: row.modelData.name; font.weight: Font.Medium }
                        TxLabel { Layout.fillWidth: true; text: row.modelData.where; color: Theme.textMuted; font.pixelSize: Theme.smallSize; elide: Text.ElideMiddle }
                        Item { Layout.fillHeight: true }
                    }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.projectChosen(row.modelData.path) }
                }
            }
        }
    }
}
