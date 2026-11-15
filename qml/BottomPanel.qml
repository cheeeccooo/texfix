import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Il pannello in basso: gli avvisi dell'ultima compilazione. Un clic porta al file e alla riga.
Rectangle {
    id: root
    color: Theme.panel
    signal issueChosen(string file, int line)
    signal closeAsked()
    signal fixAsked()

    function label(kind) {
        return kind === "wide" ? "Riga troppo larga" : kind === "loose" ? "Riga troppo vuota" : "Errore"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gap + 4
            Layout.rightMargin: 6
            implicitHeight: Theme.rowHeight + 2
            TxLabel {
                Layout.fillWidth: true
                text: Workspace.issues.length === 0 ? "Avvisi" : "Avvisi (" + Workspace.issues.length + ")"
                color: Theme.textMuted
                font.pixelSize: Theme.smallSize
                font.capitalization: Font.AllUppercase
            }
            TxButton {
                visible: Workspace.issues.length > 0
                text: "Risolvi tutto"
                hint: "Sistema errori e righe fuori misura delle lezioni"
                enabled: !Workspace.busy
                implicitHeight: Theme.rowHeight - 4
                onClicked: root.fixAsked()
            }
            TxButton { text: "×"; flat: true; implicitWidth: 24; hint: "Chiudi"; onClicked: root.closeAsked() }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Workspace.issues
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width
                height: Theme.rowHeight
                color: area.containsMouse ? Theme.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.gap + 4
                    anchors.rightMargin: Theme.gap + 4
                    spacing: Theme.gap
                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: row.modelData.kind === "error" ? Theme.bad : Theme.warn }
                    TxLabel { text: root.label(row.modelData.kind); font.weight: Font.Medium }
                    TxLabel { Layout.fillWidth: true; text: row.modelData.text; color: Theme.textMuted }
                    TxLabel {
                        visible: row.modelData.file !== ""
                        text: row.modelData.file.substring(row.modelData.file.lastIndexOf("/") + 1) + (row.modelData.line > 0 ? ", riga " + row.modelData.line : "")
                        color: Theme.textFaint
                        font.pixelSize: Theme.smallSize
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: if (row.modelData.file !== "") root.issueChosen(row.modelData.file, row.modelData.line)
                }
            }
        }
    }
    TxLabel {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: Theme.rowHeight / 2
        visible: Workspace.issues.length === 0
        text: "Nessun avviso."
        color: Theme.textFaint
    }
    Rectangle { width: parent.width; height: 1; color: Theme.border }
}
