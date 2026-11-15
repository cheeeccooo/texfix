import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Rivedi figure: tutte le figure del progetto, lezione per lezione. Di ognuna la resa (o il segnaposto), e i due modi
// di farla: ridisegnata, o ritagliata dalla fonte.
Rectangle {
    id: root
    property var items: []
    signal lineAsked(string file, int line)
    color: Theme.window

    property int revision: 0                           // le immagini si rileggono dopo ogni lavoro
    function reload() { items = Workspace.figureItems(); revision++ }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onFilesChanged() { if (root.visible) root.reload() } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TxLabel {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            text: "Rivedi figure"
            font.pixelSize: Theme.textSize + 3
            font.weight: Font.DemiBold
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.gap
            bottomMargin: 2 * Theme.gap
            model: root.items
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                required property var modelData
                x: Theme.gap + 4
                width: Math.min(list.width - 2 * Theme.gap - 8, 860)
                sourceComponent: modelData.type === "lesson" ? head : card
                onLoaded: item.row = modelData
            }
        }
    }
    TxLabel { anchors.centerIn: parent; visible: root.items.length === 0; text: "Il progetto non ha figure."; color: Theme.textFaint }

    Component {
        id: head
        TxLabel { property var row: ({}); topPadding: Theme.gap; text: row.title || ""; font.weight: Font.DemiBold }
    }
    Component {
        id: card
        Rectangle {
            id: box
            property var row: ({})
            implicitHeight: column.implicitHeight + 2 * Theme.gap + 4
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: column
                anchors.fill: parent
                anchors.margins: Theme.gap + 2
                spacing: Theme.gap
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxLabel { text: box.row.name || ""; font.weight: Font.Medium }
                    TxLabel {
                        Layout.fillWidth: true
                        text: box.row.state === "disegnata" ? "✓ Ridisegnata" : box.row.state === "ritagliata" ? "✓ Ritagliata dalla fonte" : "da inserire"
                        color: box.row.state === "da_inserire" ? Theme.warn : Theme.textFaint
                        font.pixelSize: Theme.smallSize
                    }
                    TxButton { text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(box.row.lesson, box.row.line) }
                }
                TxLabel { Layout.fillWidth: true; text: box.row.description || ""; color: Theme.textMuted; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: picture.status === Image.Ready ? Math.min(picture.sourceSize.height / 2 + 16, 320) : 0
                    visible: box.row.preview !== undefined && box.row.preview !== ""
                    color: "white"
                    radius: Theme.radius - 2
                    Image {
                        id: picture
                        anchors.fill: parent
                        anchors.margins: 8
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: true
                        source: box.row.preview ? "file://" + encodeURI(box.row.preview) + "?" + root.revision : ""
                    }
                }
                TxLabel {
                    Layout.fillWidth: true
                    visible: (box.row.note || "") !== ""
                    text: box.row.note || ""
                    color: Theme.bad
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxLabel { Layout.fillWidth: true; text: (box.row.source || "") + (box.row.page > 0 ? ", pagina " + box.row.page : ""); color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                    TxButton { text: box.row.state === "disegnata" ? "Ridisegna di nuovo" : "Ridisegna"; enabled: !Workspace.busy; onClicked: Workspace.redoFigure(box.row.id, "ridisegna") }
                    TxButton { text: "Ritaglia dalla fonte"; enabled: !Workspace.busy; onClicked: Workspace.redoFigure(box.row.id, "ritaglia") }
                }
            }
        }
    }
}
