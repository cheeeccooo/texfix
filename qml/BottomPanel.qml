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
    property int tab: 0                                // 0 Avvisi, 1 Console, 2 Attività
    // Una riga per ogni lavoro: ciò che gira (con la barra e «Ferma»), ciò che aspetta, ciò che è finito da poco.
    readonly property var activities: {
        const out = []
        if (Workspace.compiling)
            out.push({ title: "Compilazione", detail: Workspace.progressLabel, progress: Workspace.progress, state: "run", stop: true })
        if (Workspace.jobRunning)
            out.push({ title: Workspace.jobTitle, detail: Workspace.jobLabel, progress: Workspace.jobProgress, state: "run", stop: true })
        if (Chat.running)
            out.push({ title: "Agente" + (Chat.title !== "" ? ": " + Chat.title : ""), detail: Chat.waiting ? "aspetta la tua conferma" : "sta lavorando", progress: -1, state: "run", agent: true })
        if (Workspace.waitingProcedures > 0)
            out.push({ title: "Procedure dell'agente in attesa", detail: Workspace.waitingProcedures + (Workspace.waitingProcedures === 1 ? " aspetta il suo turno" : " aspettano il loro turno"), progress: -1, state: "wait" })
        if (Workspace.resumable)
            out.push({ title: "Converti interrotto", detail: "riprende senza rifare ciò che è già fatto", progress: -1, state: "wait", resume: true })
        for (const job of Workspace.recentJobs)
            out.push({ title: job.title, detail: (job.stopped ? "fermato alle " : "finito alle ") + job.when, progress: -1, state: "done" })
        return out
    }
    readonly property int running: activities.filter(a => a.state === "run").length
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
            Repeater {
                model: [Workspace.issues.length === 0 ? "Avvisi" : "Avvisi (" + Workspace.issues.length + ")", "Console", root.running === 0 ? "Attività" : "Attività (" + root.running + ")"]
                TxLabel {
                    required property string modelData
                    required property int index
                    text: modelData
                    color: root.tab === index ? Theme.text : Theme.textFaint
                    font.pixelSize: Theme.smallSize
                    font.capitalization: Font.AllUppercase
                    rightPadding: Theme.gap
                    MouseArea { anchors.fill: parent; onClicked: root.tab = index }
                }
            }
            Item { Layout.fillWidth: true }
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
        // le Attività
        ListView {
            id: activityList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab === 2 && root.activities.length > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.activities
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: activity
                required property var modelData
                width: activityList.width
                height: Theme.rowHeight + 4
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.gap + 4
                    anchors.rightMargin: Theme.gap + 4
                    spacing: Theme.gap
                    Rectangle {
                        implicitWidth: 8; implicitHeight: 8; radius: 4
                        color: activity.modelData.state === "run" ? Theme.accent : activity.modelData.state === "wait" ? Theme.warn : Theme.idle
                    }
                    TxLabel { text: activity.modelData.title; font.weight: Font.Medium; Layout.maximumWidth: activity.width * 0.35 }
                    // la barra: quanto è fatto, o un tratto pieno a metà se non si sa
                    Rectangle {
                        visible: activity.modelData.state === "run"
                        implicitWidth: 140; implicitHeight: 5; radius: 2.5
                        color: Theme.border
                        Rectangle {
                            width: parent.width * (activity.modelData.progress >= 0 ? Math.max(0.02, Math.min(1, activity.modelData.progress)) : 0.35)
                            height: parent.height
                            radius: 2.5
                            color: Theme.accent
                            opacity: activity.modelData.progress >= 0 ? 1 : 0.5
                        }
                    }
                    TxLabel { visible: activity.modelData.state === "run" && activity.modelData.progress >= 0; text: Math.round(activity.modelData.progress * 100) + "%"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    TxLabel { Layout.fillWidth: true; text: activity.modelData.detail || ""; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                    TxButton { visible: activity.modelData.stop === true; text: "Ferma"; implicitHeight: Theme.rowHeight - 4; onClicked: Workspace.stop() }
                    TxButton { visible: activity.modelData.agent === true; text: "Ferma"; implicitHeight: Theme.rowHeight - 4; onClicked: Chat.stop() }
                    TxButton { visible: activity.modelData.resume === true; text: "Riprendi"; implicitHeight: Theme.rowHeight - 4; enabled: !Workspace.busy; onClicked: Workspace.resume() }
                }
            }
        }
        TxLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab === 2 && root.activities.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: "Nessun lavoro."
            color: Theme.textFaint
        }
        // la Console: il registro dei messaggi e dei lavori
        ListView {
            id: consoleList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab === 1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Workspace.console
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: Qt.callLater(positionViewAtEnd)
            delegate: Text {
                required property string modelData
                x: Theme.gap + 4
                width: consoleList.width - 2 * Theme.gap - 8
                text: modelData
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.monoFamily
                font.pixelSize: Theme.smallSize
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab === 0
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
        visible: root.tab === 0 && Workspace.issues.length === 0
        text: "Nessun avviso."
        color: Theme.textFaint
    }
    Rectangle { width: parent.width; height: 1; color: Theme.border }
}
