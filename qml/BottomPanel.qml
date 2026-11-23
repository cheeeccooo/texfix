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
        const run = Workspace.run
        if (Workspace.jobRunning)
            out.push({ title: Workspace.jobTitle, detail: Workspace.pausedForConnection ? "la connessione non è tornata: finisco ciò che è in corso e mi fermo"
                                                        : Workspace.pausedForCap ? "utilizzo al tetto dei lavori (" + Workspace.capPercent + "%): finisco ciò che è in corso e mi fermo"
                                                        : Workspace.pausedUntil !== "" ? "limite di utilizzo raggiunto: finisco ciò che è in corso e mi fermo"
                                                        : Workspace.offline ? "manca la connessione: aspetto che torni"
                                                        : Workspace.jobLabel + (Workspace.usage.runsOut !== undefined ? " · a questo ritmo il limite di Claude arriva " + Workspace.usage.runsOut : ""),
                       progress: run.id && Workspace.jobTitle === run.title ? run.progress : Workspace.jobProgress, state: Workspace.pausedUntil !== "" || Workspace.offline ? "wait" : "run", stop: true,
                       open: run.id && Workspace.jobTitle === run.title ? "lavoro:" + run.id : "" })
        if (Chat.running)
            out.push({ title: "Agente" + (Chat.title !== "" ? ": " + Chat.title : ""), detail: Chat.waiting ? "aspetta la tua conferma" : "sta lavorando", progress: -1, state: "run", agent: true })
        // il lavoro di «Avvia» fermo che aspetta la finestra: una riga sola, quella dell'attesa (riparte da sé, «Adesso», «Apri»)
        const runWaiting = Workspace.waitingLimit.some(w => w.key === "lavoro:" + run.id)
        if (run.id && !Workspace.jobRunning && run.state !== "finito" && !runWaiting)      // il lavoro di «Avvia» fermato o interrotto
            out.push({ title: run.title, detail: (run.state === "fermato" ? (run.connection ? "in pausa perché mancava la connessione" : run.cap ? "in pausa al tetto dei lavori" : run.until !== "" ? "in pausa per il limite di utilizzo" : "in pausa") : "interrotto")
                               + (run.until !== "" && !run.untilPassed && !run.connection ? ": riprende da solo " + run.until : ": riprende senza rifare ciò che è già fatto"),
                       progress: -1, state: "wait", resume: true, open: "lavoro:" + run.id })
        else if (run.id && run.state === "finito")
            out.push({ title: run.title, detail: "finito: l'esito è da leggere", progress: -1, state: "done", open: "lavoro:" + run.id })
        else if (Workspace.resumable && !runWaiting)
            out.push({ title: "Scrittura delle lezioni interrotta", detail: "riprende senza rifare ciò che è già fatto", progress: -1, state: "wait", resume: true })
        // ciò che aspetta il limite di Claude: riparte da sé quando la finestra si azzera
        const waiting = Workspace.waitingLimit
        for (let i = 0; i < waiting.length; ++i)
            out.push({ title: waiting[i].title, detail: (waiting[i].cap ? "l'utilizzo è al tetto dei lavori (" + Workspace.capPercent + "%)" : "aspetta il limite di utilizzo di Claude")
                                                                + ": riparte da sé " + waiting[i].until, progress: -1, state: "wait", later: i,
                       open: waiting[i].key.startsWith("lavoro:") ? waiting[i].key : "" })
        const unread = Workspace.queue.filter(q => q.state === "errore").length
        if (unread > 0)
            out.push({ title: "Materiale non riconosciuto", detail: (unread === 1 ? "un file" : unread + " file") + " in arrivo: il perché accanto, poi «Riprova»", progress: -1, state: "wait" })
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
                // solo per gli avvisi delle lezioni: una compilazione che non è partita non si risolve con il modello
                visible: Workspace.issues.some(i => i.file !== "")
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
                    TxButton { visible: (activity.modelData.open || "") !== ""; text: "Apri"; implicitHeight: Theme.rowHeight - 4; onClicked: root.issueChosen(activity.modelData.open, 0) }
                    TxButton { visible: activity.modelData.stop === true; text: "Ferma"; implicitHeight: Theme.rowHeight - 4; onClicked: Workspace.stop() }
                    TxButton { visible: activity.modelData.agent === true; text: "Ferma"; implicitHeight: Theme.rowHeight - 4; onClicked: Chat.stop() }
                    TxButton { visible: activity.modelData.resume === true; text: "Riprendi"; implicitHeight: Theme.rowHeight - 4; enabled: !Workspace.busy; onClicked: Workspace.resume() }
                    TxButton { visible: activity.modelData.later !== undefined; text: "Adesso"; implicitHeight: Theme.rowHeight - 4; enabled: !Workspace.busy; hint: "Prova subito: se il limite c'è ancora, torna ad aspettare"; onClicked: Workspace.retryLater(activity.modelData.later) }
                    TxButton { visible: activity.modelData.later !== undefined; text: "Lascia stare"; implicitHeight: Theme.rowHeight - 4; onClicked: Workspace.dropLater(activity.modelData.later) }
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
