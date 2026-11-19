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
    // una compilazione chiesta dall'utente si vede anche mentre un lavoro gira
    readonly property bool ownRun: Workspace.jobTitle === "Aggiungi materiale" && Workspace.run.id !== undefined
    readonly property real fraction: Workspace.compiling ? Workspace.progress : ownRun ? Workspace.run.progress : Workspace.jobProgress
    readonly property string activity: Workspace.compiling
        ? "Compilazione" + (Workspace.progressLabel !== "" ? ": " + Workspace.progressLabel : "")
        : Workspace.pausedForConnection ? Workspace.jobTitle + ": la connessione non è tornata, finisco ciò che è in corso e mi fermo"
        : Workspace.pausedUntil !== "" ? Workspace.jobTitle + ": limite di utilizzo raggiunto, finisco ciò che è in corso e mi fermo"
        : Workspace.offline ? Workspace.jobTitle + ": manca la connessione, aspetto che torni"
        : Workspace.jobTitle + (Workspace.jobLabel !== "" ? ": " + Workspace.jobLabel : "")
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
        // lo stato: mentre si lavora un anello fermo con un tratto che gira dentro; altrimenti un segno
        Item {
            implicitWidth: 20
            implicitHeight: 20
            Text {
                anchors.centerIn: parent
                visible: !root.working
                text: Workspace.errorCount > 0 ? "×" : Workspace.issues.length > 0 ? "△" : "✓"
                color: Workspace.errorCount > 0 ? Theme.bad : Workspace.issues.length > 0 ? Theme.warn : Theme.good
                font.pixelSize: Theme.textSize + 2
                font.weight: Font.Bold
            }
            Rectangle {                                // l'anello, fermo
                anchors.centerIn: parent
                visible: root.working
                width: 18; height: 18; radius: 9
                color: "transparent"
                border.width: 2.5
                border.color: Theme.border
                Item {                                 // il tratto, che gira
                    anchors.fill: parent
                    Rectangle { x: parent.width / 2 - 2.5; y: -1; width: 5; height: 5; radius: 2.5; color: Theme.accent }
                    Rectangle { x: parent.width / 2 - 2.5; y: -1; width: 5; height: 5; radius: 2.5; color: Theme.accent; opacity: 0.55; transformOrigin: Item.Center; transform: Rotation { origin.x: 2.5; origin.y: 10; angle: -40 } }
                    Rectangle { x: parent.width / 2 - 2.5; y: -1; width: 5; height: 5; radius: 2.5; color: Theme.accent; opacity: 0.25; transform: Rotation { origin.x: 2.5; origin.y: 10; angle: -80 } }
                    RotationAnimator on rotation { running: root.working; from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
                }
            }
        }
        TxLabel { Layout.fillWidth: true; text: root.messageText; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
        TxLabel {
            visible: root.working
            text: root.activity
            Layout.maximumWidth: root.width / 2
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
        }
        // la barra: quanto è fatto; finché non si sa, un tratto che scorre
        Rectangle {
            id: bar
            visible: root.working
            implicitWidth: 200; implicitHeight: 6; radius: 3
            color: Theme.border
            clip: true
            readonly property bool unknown: root.fraction < 0.02
            Rectangle { visible: !bar.unknown; width: parent.width * Math.min(1, root.fraction); height: parent.height; radius: 3; color: Theme.accent }
            Rectangle {
                visible: bar.unknown
                width: parent.width * 0.3; height: parent.height; radius: 3
                color: Theme.accent
                NumberAnimation on x { running: bar.visible && bar.unknown; from: -60; to: 200; duration: 1100; loops: Animation.Infinite }
            }
        }
        TxLabel {
            visible: root.working && root.fraction >= 0.02
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
