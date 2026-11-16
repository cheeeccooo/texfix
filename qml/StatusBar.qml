import QtQuick
import QtQuick.Layouts
import Texfix

// La barra di stato: lo stato, i messaggi brevi, l'avanzamento del lavoro in corso con «Ferma».
Rectangle {
    id: root
    implicitHeight: Theme.rowHeight
    color: Theme.window
    signal clicked()                                   // un clic riapre il pannello in basso

    property string messageText
    readonly property bool working: Workspace.busy
    readonly property real fraction: Workspace.jobRunning ? Workspace.jobProgress : Workspace.progress
    readonly property string activity: Workspace.jobRunning
        ? Workspace.jobTitle + (Workspace.jobLabel !== "" ? ": " + Workspace.jobLabel : "")
        : "Compilazione" + (Workspace.progressLabel !== "" ? ": " + Workspace.progressLabel : "")
    Connections {
        target: Workspace
        function onMessage(text) {
            // la prima frase, per 6 secondi
            const end = text.search(/[.!?](\s|$)/)
            root.messageText = end >= 0 ? text.substring(0, end + 1) : text
            fade.restart()
        }
    }
    Timer { id: fade; interval: 6000; onTriggered: root.messageText = "" }

    Rectangle { width: parent.width; height: 1; color: Theme.border }
    MouseArea { anchors.fill: parent; onClicked: root.clicked() }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.gap + 4
        anchors.rightMargin: Theme.gap
        spacing: Theme.gap
        Text {
            id: state
            text: root.working ? "◌" : Workspace.errorCount > 0 ? "×" : Workspace.issues.length > 0 ? "△" : "✓"
            color: root.working ? Theme.accent : Workspace.errorCount > 0 ? Theme.bad : Workspace.issues.length > 0 ? Theme.warn : Theme.good
            font.pixelSize: root.working ? Theme.textSize + 7 : Theme.textSize + 2
            Layout.preferredWidth: Theme.textSize + 8
            horizontalAlignment: Text.AlignHCenter
            font.weight: Font.Bold
            RotationAnimator on rotation { id: spin; running: root.working; from: 0; to: 360; duration: 1200; loops: Animation.Infinite }
            Connections { target: root; function onWorkingChanged() { if (!root.working) { spin.stop(); state.rotation = 0 } } }
        }
        TxLabel { Layout.fillWidth: true; text: root.messageText; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
        TxLabel {
            visible: root.working
            text: root.activity
            Layout.maximumWidth: root.width / 2
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
        }
        Rectangle {
            visible: root.working
            implicitWidth: 120; implicitHeight: 4; radius: 2
            color: Theme.border
            Rectangle { width: parent.width * root.fraction; height: parent.height; radius: 2; color: Theme.accent }
        }
        TxLabel {
            visible: root.working
            text: Math.round(root.fraction * 100) + "%"
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 30
        }
        TxButton { visible: Workspace.resumable && !root.working; text: "Riprendi"; hint: "Riprende il lavoro interrotto, senza rifare ciò che è già fatto"; implicitHeight: Theme.rowHeight - 6; onClicked: Workspace.resume() }
        TxButton { visible: root.working; text: "Ferma"; implicitHeight: Theme.rowHeight - 6; onClicked: Workspace.stop() }
    }
}
