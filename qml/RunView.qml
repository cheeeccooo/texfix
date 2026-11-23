import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Il lavoro di «Avvia», a tutta finestra come le revisioni. In alto lo stato (in corso, in pausa per il limite di
// utilizzo, fermato, finito), quanto è fatto, il tempo, il costo, i tasti. A sinistra i cantieri: ogni lezione da scrivere
// con le sue fasi, ogni lezione del corso che l'agente completa, e di ognuno che cosa fa ora. Al centro l'attività: le
// chiamate ai sottoagenti e i passi senza modello (compilazioni, prove), quelli in corso in cima; un clic su una chiamata
// apre a destra che cosa ha ricevuto, che cosa ha fatto e che cosa ha risposto. Alla fine, l'esito.
Rectangle {
    id: root
    color: Theme.window
    property string planId
    signal closeAsked()
    signal lineAsked(string file, int line)
    signal reviewAsked(string what)                    // «rivedi:dubbi»…

    readonly property var run: Workspace.run
    readonly property bool mine: run.id === planId
    readonly property string runState: mine ? run.state : ""
    readonly property bool working: runState === "in_corso" || runState === "in_pausa"
    property string siteFilter: ""                     // un cantiere scelto: solo le sue chiamate
    property bool onlyCalls: false                     // l'attività senza compilazioni e prove
    property int chosen: -1                            // la chiamata aperta
    property var detail: ({})
    property bool confirmUndo: false
    property real now: Date.now()
    readonly property bool narrow: width < 980

    Timer { interval: 1000; repeat: true; running: root.visible && root.working; onTriggered: { root.now = Date.now(); root.refreshDetail() } }
    function refreshDetail() { detail = chosen >= 0 ? Workspace.runCall(chosen) : ({}) }
    onChosenChanged: refreshDetail()
    function showCall(n) {                             // per le prove: apre la chiamata n (o la prima)
        const calls = run.calls || []
        if (calls.length > 0)
            chosen = n > 0 ? n : calls[0].id
    }

    function money(x) { return (x || 0).toFixed(2).replace(".", ",") + " $" }
    function minutes(ms) {
        const m = Math.round(ms / 60000)
        return m < 1 ? "meno di un minuto" : m === 1 ? "un minuto" : m < 60 ? m + " minuti" : Math.floor(m / 60) + " h " + (m % 60) + " min"
    }
    function seconds(call) {
        const s = call.state === "in_corso" || call.state === "riprova" || call.state === "connessione" ? (now - call.started) / 1000 : call.seconds || 0
        return s < 60 ? Math.round(s) + " s" : Math.floor(s / 60) + " min " + Math.round(s % 60) + " s"
    }
    function stateColor(s) {
        return s === "fatta" || s === "fatto" ? Theme.good : s === "fallita" ? Theme.bad : s === "in_corso" || s === "riprova" ? Theme.accent
             : s === "limite" || s === "fallito" || s === "connessione" ? Theme.warn : s === "saltata" ? Theme.textFaint : Theme.idle
    }
    function callState(s) {
        return s === "in_corso" ? "in corso" : s === "riprova" ? "riprova" : s === "fatta" ? "fatta" : s === "fallita" ? "non riuscita"
             : s === "limite" ? "limite di utilizzo: aspetta" : s === "connessione" ? "aspetta la connessione" : s === "fermata" ? "fermata" : s === "interrotta" ? "interrotta" : s
    }
    function kindLabel(kind) {
        return kind === "converti" ? "Lezione nuova" : kind === "integra" ? "Aggiunte a una lezione del corso"
             : kind === "versione" ? "Versione nuova di una fonte" : kind === "fondi" ? "Lezione nuova, scritta dall'agente"
             : kind === "piano" ? "Una parte del piano" : ""
    }
    readonly property string stateText: {
        if (!mine)
            return "Questo lavoro non c'è più: è stato archiviato o annullato."
        if (runState === "in_corso")
            return run.offline ? "Manca la connessione: le chiamate aspettano che torni, poi si rifanno. Se non torna entro dieci minuti mi fermo." : "In corso"
        if (runState === "in_pausa")
            return run.connection ? "La connessione non è tornata: finisco ciò che è in corso, poi mi fermo. Si riprende quando c'è."
                                  : "Limite di utilizzo di Claude raggiunto: finisco ciò che è in corso, poi mi fermo. Si potrà riprendere " + run.until + "."
        const limited = run.connection ? "Quando la connessione c'è, si riprende."
                      : run.until !== "" ? (run.untilPassed ? "Il limite di utilizzo è passato: si può riprendere."
                                                             : "Si può riprendere " + run.until + ".") : ""
        const why = run.connection ? "perché mancava la connessione" : "dal limite di utilizzo di Claude"
        if (runState === "fermato")
            return limited !== "" ? "In pausa " + why + ". " + limited
                                  : "In pausa: ciò che è fatto resta, anche a metà. «Riprendi» continua da lì; «Annulla tutto il lavoro» riporta il progetto a prima di «Avvia»."
        if (runState === "interrotto")
            return "Interrotto dalla chiusura del programma." + (limited !== "" ? " Era fermo " + why + ". " + limited : "")
        return "Finito"
    }
    // il tempo lavorato (senza le soste) e quello che manca, dal ritmo fin qui
    readonly property real worked: (run.worked || 0) + (run.sessionStart > 0 ? now - run.sessionStart : 0)
    readonly property real progressNow: runState === "finito" ? 1 : run.progress || 0
    readonly property real remaining: progressNow >= 0.03 && progressNow < 1 && worked > 0 ? worked * (1 - progressNow) / progressNow : 0
    readonly property var calls: (run.calls || []).filter(c => siteFilter === "" || c.site === siteFilter)
    // chiamate e passi (compilazioni, prove, preparazione), del cantiere scelto o di tutti
    readonly property var activity: (run.activity || []).filter(a => (siteFilter === "" || a.site === siteFilter) && (!onlyCalls || a.kind === "chiamata"))
    readonly property int stepsShown: (run.activity || []).filter(a => a.kind === "passo" && (siteFilter === "" || a.site === siteFilter)).length

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // in alto: che cosa è, a che punto è, i tasti
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            Layout.leftMargin: 2 * Theme.gap + 4
            Layout.rightMargin: 2 * Theme.gap + 4
            spacing: Theme.gap
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TxLabel { Layout.maximumWidth: root.width * 0.55; text: root.mine ? Workspace.run.title : "Il lavoro"; font.pixelSize: Theme.textSize + 3; font.weight: Font.DemiBold }
                Rectangle {
                    visible: root.mine
                    implicitWidth: stateLabel.implicitWidth + 14
                    implicitHeight: stateLabel.implicitHeight + 6
                    radius: 3
                    readonly property color tone: root.runState === "finito" ? Theme.good : root.runState === "in_pausa" ? Theme.warn
                                                : root.runState === "in_corso" ? Theme.accent : Theme.idle
                    color: Qt.rgba(tone.r, tone.g, tone.b, 0.25)
                    Text {
                        id: stateLabel
                        anchors.centerIn: parent
                        text: root.runState === "in_corso" ? "in corso" : root.runState === "in_pausa" ? "si sta fermando" : root.runState === "fermato" ? "in pausa" : root.runState
                        color: Theme.text
                        font.pixelSize: Theme.smallSize
                        font.weight: Font.DemiBold
                    }
                }
                Item { Layout.fillWidth: true }
                TxButton { visible: root.working; text: "Metti in pausa"; hint: "Ciò che è fatto resta, anche a metà: «Riprendi» continua da lì"; onClicked: Workspace.stop() }
                TxButton {
                    visible: root.runState === "fermato" || root.runState === "interrotto"
                    text: "Riprendi"
                    primary: true
                    enabled: !Workspace.busy
                    hint: Workspace.busy ? "Aspetta che finisca il lavoro in corso" : "Riprende senza rifare ciò che è fatto"
                    onClicked: Workspace.resumeRun()
                }
                TxButton {
                    visible: root.mine && !root.working && root.run.canUndo === true && !root.confirmUndo
                    text: "Annulla tutto il lavoro"
                    enabled: !Workspace.busy
                    onClicked: root.confirmUndo = true
                }
                TxButton { visible: root.runState === "finito"; text: "Chiudi e archivia"; primary: true; hint: "L'esito va fra gli esiti; i registri delle chiamate si buttano"; onClicked: { Workspace.closeRun(); root.closeAsked() } }
            }
            // la conferma di «Annulla tutto il lavoro»
            Rectangle {
                Layout.fillWidth: true
                visible: root.confirmUndo
                implicitHeight: undoRow.implicitHeight + 2 * Theme.gap
                radius: Theme.radius
                color: Qt.rgba(Theme.bad.r, Theme.bad.g, Theme.bad.b, 0.12)
                RowLayout {
                    id: undoRow
                    anchors.fill: parent
                    anchors.margins: Theme.gap
                    spacing: Theme.gap
                    TxLabel {
                        Layout.fillWidth: true
                        text: "Il progetto torna com'era prima di «Avvia»: lezioni, fonti, dubbi e figure, anche ciò che hai cambiato dopo. Il piano torna da avviare."
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                    TxButton { text: "No"; onClicked: root.confirmUndo = false }
                    TxButton { text: "Annulla tutto"; primary: true; onClicked: { root.confirmUndo = false; Workspace.undoRun(); root.closeAsked() } }
                }
            }
            // la barra di tutto il lavoro, e i numeri
            RowLayout {
                Layout.fillWidth: true
                visible: root.mine
                spacing: 2 * Theme.gap
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 8
                    radius: 4
                    color: Theme.border
                    Rectangle {
                        width: parent.width * Math.min(1, root.runState === "finito" ? 1 : root.run.progress || 0)
                        height: parent.height
                        radius: 4
                        color: root.runState === "in_pausa" ? Theme.warn : root.runState === "finito" ? Theme.good : Theme.accent
                    }
                }
                TxLabel { text: Math.round(100 * (root.runState === "finito" ? 1 : root.run.progress || 0)) + "%"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            }
            TxLabel {
                Layout.fillWidth: true
                visible: root.mine
                text: [root.stateText,
                       "Tempo: " + root.minutes(root.worked),
                       root.remaining > 0 ? "Manca: " + (root.remaining < 60000 ? "meno di un minuto" : "circa " + root.minutes(root.remaining)) : root.working ? "Manca: lo stimo appena qualcosa è fatto" : "",
                       "Costo: " + root.money(root.run.cost) + (root.run.estimate > 0 ? " (stima " + root.money(root.run.estimate)
                           + (root.runState !== "finito" && root.run.estimate > root.run.cost ? ", ne resta circa " + root.money(root.run.estimate - root.run.cost) : "") + ")" : ""),
                       root.working ? "Sottoagenti al lavoro: " + root.run.active : ""].filter(s => s !== "").join("   ·   ")
                color: root.runState === "in_pausa" ? Theme.warn : Theme.textMuted
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            // l'utilizzo di Claude mentre il lavoro gira: a questo ritmo quando arriva il limite, e se prima della fine
            TxLabel {
                readonly property var u: Workspace.usage
                readonly property bool before: u.runsOutMinutes !== undefined && root.remaining > 0 && u.runsOutMinutes * 60000 < root.remaining
                Layout.fillWidth: true
                visible: root.mine && root.working && u.known === true
                text: u.runsOut !== undefined
                      ? "Claude: " + u.hours.used + "% delle cinque ore; a questo ritmo il limite arriva " + u.runsOut
                        + (before ? ", prima della fine del lavoro: lì il lavoro va in pausa da sé e riprende con un clic."
                                  : root.remaining > 0 ? ", dopo la fine del lavoro." : ".")
                      : "Claude: " + u.hours.used + "% delle cinque ore" + (u.hours.resets !== "" ? ", si azzera " + u.hours.resets : "") + "."
                color: before ? Theme.warn : Theme.textMuted
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            // adesso: che cosa fa ogni cantiere al lavoro (un clic mostra solo il suo)
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.working && (root.run.now || []).length > 0
                spacing: 2
                Repeater {
                    model: root.working ? (root.run.now || []) : []
                    RowLayout {
                        id: nowRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        TxLabel {
                            Layout.preferredWidth: 70
                            text: nowRow.index === 0 ? "Adesso" : ""
                            font.pixelSize: Theme.smallSize
                            font.weight: Font.DemiBold
                            color: Theme.textMuted
                        }
                        Rectangle {
                            implicitWidth: 7; implicitHeight: 7; radius: 3.5
                            color: Theme.accent
                            SequentialAnimation on opacity {
                                running: root.visible && root.runState === "in_corso"
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 600 }
                                NumberAnimation { to: 1; duration: 600 }
                            }
                        }
                        TxLabel {
                            Layout.fillWidth: true
                            text: nowRow.modelData.title + ":  " + nowRow.modelData.text
                            font.pixelSize: Theme.smallSize
                            MouseArea {
                                anchors.fill: parent
                                enabled: nowRow.modelData.key !== ""
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.siteFilter = nowRow.modelData.key
                            }
                        }
                    }
                }
            }
            // l'esito, quando c'è
            Rectangle {
                Layout.fillWidth: true
                visible: root.mine && !root.working && ((root.run.outcome || {}).lines || []).length > 0
                implicitHeight: outcomeColumn.implicitHeight + 2 * Theme.gap
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                ColumnLayout {
                    id: outcomeColumn
                    anchors.fill: parent
                    anchors.margins: Theme.gap
                    spacing: 4
                    Repeater {
                        model: (root.run.outcome || {}).lines || []
                        TxLabel {
                            required property string modelData
                            Layout.fillWidth: true
                            text: "•  " + modelData
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }
                    RowLayout {
                        visible: root.runState === "finito"
                        spacing: Theme.gap
                        TxButton { visible: Workspace.openDoubts > 0; text: "Rivedi dubbi  " + Workspace.openDoubts; onClicked: root.reviewAsked("rivedi:dubbi") }
                        TxButton { visible: Workspace.openDeclared > 0; text: "Rivedi correzioni  " + Workspace.openDeclared; onClicked: root.reviewAsked("rivedi:correzioni") }
                        TxButton { visible: Workspace.openFigures > 0; text: "Rivedi figure  " + Workspace.openFigures; onClicked: root.reviewAsked("rivedi:figure") }
                        TxButton { visible: Workspace.openLayout > 0; text: "Rivedi impaginazione  " + Workspace.openLayout; onClicked: root.reviewAsked("rivedi:impaginazione") }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // sotto: cantieri, chiamate e dettaglio
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mine
            columns: root.narrow ? 1 : 3
            rowSpacing: 0
            columnSpacing: 0

            // i cantieri
            TxScroll {
                id: sitesScroll
                Layout.fillWidth: root.narrow
                Layout.preferredWidth: root.narrow ? -1 : Math.min(380, root.width * 0.3)
                Layout.preferredHeight: root.narrow ? Math.min(220, sitesColumn.implicitHeight + 2 * Theme.gap) : -1
                Layout.fillHeight: !root.narrow
                visible: !(root.narrow && root.chosen >= 0)
                contentHeight: sitesColumn.implicitHeight + 2 * Theme.gap
                Column {
                    id: sitesColumn
                    x: 2 * Theme.gap
                    y: Theme.gap
                    width: sitesScroll.width - 3 * Theme.gap
                    spacing: Theme.gap
                    RowLayout {
                        width: parent.width
                        TxLabel { Layout.fillWidth: true; text: "Cantieri  " + (root.run.sites || []).length; font.weight: Font.DemiBold; color: Theme.textMuted }
                        TxButton { visible: root.siteFilter !== ""; text: "Tutti"; flat: true; implicitHeight: Theme.rowHeight - 4; onClicked: root.siteFilter = "" }
                    }
                    Repeater {
                        model: root.run.sites || []
                        Rectangle {
                            id: siteCard
                            required property var modelData
                            readonly property bool picked: root.siteFilter === modelData.key
                            width: sitesColumn.width
                            implicitHeight: siteColumn.implicitHeight + 2 * Theme.gap
                            radius: Theme.radius
                            color: siteArea.containsMouse ? Theme.hover : Theme.surface
                            border.width: picked ? 2 : 1
                            border.color: picked ? Theme.accent : Theme.border
                            opacity: modelData.state === "da_fare" ? 0.75 : 1
                            MouseArea {
                                id: siteArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.siteFilter = siteCard.picked ? "" : siteCard.modelData.key
                            }
                            ColumnLayout {
                                id: siteColumn
                                x: Theme.gap
                                y: Theme.gap
                                width: parent.width - 2 * Theme.gap
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle { implicitWidth: 9; implicitHeight: 9; radius: 4.5; color: root.stateColor(siteCard.modelData.state) }
                                    TxLabel {
                                        Layout.fillWidth: true
                                        text: siteCard.modelData.title || ""
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                    }
                                }
                                TxLabel {
                                    Layout.fillWidth: true
                                    text: root.kindLabel(siteCard.modelData.kind) + (siteCard.modelData.file ? " · " + siteCard.modelData.file : "")
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.smallSize
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Repeater {
                                        model: siteCard.modelData.phases || []
                                        Rectangle {
                                            required property var modelData
                                            implicitWidth: phaseText.implicitWidth + 12
                                            implicitHeight: phaseText.implicitHeight + 4
                                            radius: 3
                                            readonly property color tone: root.stateColor(modelData.state)
                                            color: Qt.rgba(tone.r, tone.g, tone.b, modelData.state === "da_fare" || modelData.state === "saltata" ? 0.08 : 0.22)
                                            Text {
                                                id: phaseText
                                                anchors.centerIn: parent
                                                text: parent.modelData.name + (parent.modelData.total > 0 && parent.modelData.state !== "saltata"
                                                                               ? " " + Math.min(parent.modelData.done, parent.modelData.total) + "/" + parent.modelData.total : "")
                                                color: parent.modelData.state === "da_fare" || parent.modelData.state === "saltata" ? Theme.textFaint : Theme.text
                                                font.pixelSize: Theme.smallSize - 1
                                            }
                                        }
                                    }
                                }
                                // che cosa fa ora, o perché aspetta
                                TxLabel {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: (siteCard.modelData.now || []).length > 0 ? "Ora: " + siteCard.modelData.now.join(" — ")
                                          : siteCard.modelData.state === "in_corso" && root.runState === "in_corso"
                                            ? (siteCard.modelData.kind === "converti" ? "Aspetta il suo turno: le lezioni si chiudono una alla volta" : "Aspetta il suo turno")
                                          : ""
                                    color: (siteCard.modelData.now || []).length > 0 ? Theme.accent : Theme.textFaint
                                    font.pixelSize: Theme.smallSize
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                }
                                // le fasi saltate, con il perché
                                TxLabel {
                                    Layout.fillWidth: true
                                    readonly property var skipped: (siteCard.modelData.phases || []).filter(p => p.state === "saltata" && (p.note || "") !== "")
                                    visible: skipped.length > 0
                                    text: skipped.map(p => p.name + " saltata: " + p.note).join(" · ")
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.smallSize
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                }
                                TxLabel {
                                    Layout.fillWidth: true
                                    visible: (siteCard.modelData.failure || "") !== ""
                                    text: siteCard.modelData.failure || ""
                                    color: Theme.bad
                                    font.pixelSize: Theme.smallSize
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                }
                                TxLabel {
                                    Layout.fillWidth: true
                                    visible: (siteCard.modelData.outcome || "") !== ""
                                    text: siteCard.modelData.outcome || ""
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.smallSize
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: siteCard.picked ? 40 : 4
                                }
                                TxButton {
                                    visible: siteCard.modelData.state === "fatta" && (siteCard.modelData.file || "") !== ""
                                    text: "Apri la lezione"
                                    flat: true
                                    implicitHeight: Theme.rowHeight - 4
                                    onClicked: root.lineAsked(siteCard.modelData.file, 1)
                                }
                            }
                        }
                    }
                }
            }

            // le chiamate
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !(root.narrow && root.chosen >= 0)
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.gap
                    Layout.leftMargin: 2 * Theme.gap
                    spacing: Theme.gap
                    TxLabel {
                        Layout.fillWidth: true
                        text: "Attività   " + (root.calls.length === 1 ? "1 chiamata" : root.calls.length + " chiamate ai sottoagenti")
                              + (root.stepsShown > 0 ? " · " + (root.stepsShown === 1 ? "1 passo" : root.stepsShown + " passi") + " senza modello" : "")
                              + (root.siteFilter !== "" ? " · solo " + ((root.run.sites || []).find(s => s.key === root.siteFilter) || { title: "" }).title : "")
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }
                    TxButton {
                        visible: root.stepsShown > 0
                        text: root.onlyCalls ? "Anche compilazioni e prove" : "Solo le chiamate"
                        flat: true
                        implicitHeight: Theme.rowHeight - 4
                        onClicked: root.onlyCalls = !root.onlyCalls
                    }
                }
                ListView {
                    id: callList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.activity
                    spacing: 2
                    ScrollBar.vertical: ScrollBar { policy: callList.contentHeight > callList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                    delegate: Rectangle {
                        id: callRow
                        required property var modelData
                        readonly property bool step: modelData.kind === "passo"
                        readonly property bool open: !step && root.chosen === modelData.id
                        x: 2 * Theme.gap
                        width: callList.width - 3 * Theme.gap
                        implicitHeight: callColumn.implicitHeight + 10
                        radius: Theme.radius
                        color: open ? Theme.selected : callArea.containsMouse ? Theme.hover : "transparent"
                        MouseArea {
                            id: callArea
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !callRow.step                // un passo non ha altro da mostrare
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.chosen = callRow.open ? -1 : callRow.modelData.id
                        }
                        ColumnLayout {
                            id: callColumn
                            x: 8
                            y: 5
                            width: parent.width - 16
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Rectangle {
                                    implicitWidth: 8; implicitHeight: 8; radius: callRow.step ? 1.5 : 4      // un passo: un quadratino
                                    color: root.stateColor(callRow.modelData.state)
                                    SequentialAnimation on opacity {
                                        running: callRow.modelData.state === "in_corso" && root.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 600 }
                                        NumberAnimation { to: 1; duration: 600 }
                                    }
                                }
                                TxLabel {
                                    Layout.maximumWidth: callRow.width * 0.6
                                    text: callRow.step ? callRow.modelData.text : callRow.modelData.role + (callRow.modelData.what ? " · " + callRow.modelData.what : "")
                                    font.weight: callRow.step ? Font.Normal : Font.Medium
                                    color: callRow.step ? Theme.textMuted : Theme.text
                                }
                                TxLabel {
                                    Layout.fillWidth: true
                                    text: callRow.modelData.siteTitle || ""
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.smallSize
                                }
                                TxLabel {
                                    text: [callRow.step ? "" : callRow.modelData.model, root.seconds(callRow.modelData),
                                           callRow.modelData.cost > 0 ? root.money(callRow.modelData.cost) : ""].filter(s => s !== "").join(" · ")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.smallSize
                                }
                            }
                            TxLabel {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: callRow.step ? (callRow.modelData.state === "fermato" || callRow.modelData.state === "interrotto" ? callRow.modelData.state
                                                      : callRow.modelData.outcome || "")
                                      : callRow.modelData.state === "connessione" ? "manca la connessione: si rifà appena torna"
                                      : callRow.modelData.state === "limite" ? (callRow.modelData.connection ? (root.working ? "la connessione non è tornata: si rifà alla ripresa" : "fermata perché mancava la connessione: si rifà alla ripresa")
                                                                               : root.working ? "limite di utilizzo raggiunto: si rifà alla ripresa" : "fermata dal limite di utilizzo: si rifà alla ripresa")
                                      : callRow.modelData.state === "fallita" || callRow.modelData.state === "riprova"
                                      ? root.callState(callRow.modelData.state) + (callRow.modelData.error ? ": " + callRow.modelData.error : "")
                                      : callRow.modelData.state === "in_corso" ? (callRow.modelData.last || "")
                                      : callRow.modelData.state === "fermata" || callRow.modelData.state === "interrotta" ? root.callState(callRow.modelData.state) : ""
                                color: callRow.step ? (callRow.modelData.state === "fallito" ? Theme.warn : Theme.textFaint)
                                     : callRow.modelData.state === "fallita" ? Theme.bad : callRow.modelData.state === "in_corso" ? Theme.textMuted : Theme.warn
                                font.pixelSize: Theme.smallSize
                                leftPadding: 14
                            }
                        }
                    }
                }
                TxLabel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.activity.length === 0
                    horizontalAlignment: Text.AlignHCenter
                    text: root.working ? "Chiamate e passi compaiono qui appena partono." : "Niente."
                    color: Theme.textFaint
                }
            }

            // una chiamata per intero
            Rectangle {
                Layout.fillWidth: root.narrow
                Layout.preferredWidth: root.narrow ? -1 : Math.max(380, root.width * 0.38)
                Layout.fillHeight: true
                visible: root.chosen >= 0
                color: Theme.surface
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border; visible: !root.narrow }
                readonly property var call: (root.run.calls || []).find(c => c.id === root.chosen) || null
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.gap
                    anchors.leftMargin: 2 * Theme.gap
                    spacing: Theme.gap
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        TxButton { visible: root.narrow; text: "←  Chiamate"; onClicked: root.chosen = -1 }
                        TxLabel {
                            Layout.fillWidth: true
                            text: parent.parent.parent.call ? parent.parent.parent.call.role + (parent.parent.parent.call.what ? " · " + parent.parent.parent.call.what : "") : ""
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.textSize + 1
                        }
                        TxButton { visible: !root.narrow; text: "×"; flat: true; implicitWidth: 28; hint: "Chiudi"; onClicked: root.chosen = -1 }
                    }
                    TxLabel {
                        Layout.fillWidth: true
                        readonly property var call: parent.parent.call
                        text: call ? [call.siteTitle, call.model + (call.effort ? ", impegno " + call.effort : ""), root.callState(call.state), root.seconds(call),
                                      call.cost > 0 ? root.money(call.cost) : ""].filter(s => s).join("  ·  ") : ""
                        color: Theme.textMuted
                        font.pixelSize: Theme.smallSize
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                    TxLabel {
                        Layout.fillWidth: true
                        visible: (root.detail.error || "") !== ""
                        text: root.detail.error || ""
                        color: Theme.bad
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                    TxScroll {
                        id: detailScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: detailColumn.implicitHeight + Theme.gap
                        Column {
                            id: detailColumn
                            width: detailScroll.width - 14
                            spacing: Theme.gap
                            TxLabel {
                                width: parent.width
                                visible: root.detail.found === false
                                text: "Il registro di questa chiamata non c'è più."
                                color: Theme.textFaint
                            }
                            // che cosa ha fatto (l'agente): il suo flusso
                            Section { title: "Che cosa ha fatto"; visible: (root.detail.stream || []).length > 0 }
                            Repeater {
                                model: root.detail.stream || []
                                Text {
                                    required property var modelData
                                    width: detailColumn.width
                                    text: modelData.tipo === "strumento" ? "›  " + modelData.testo : modelData.testo
                                    color: modelData.tipo === "testo" ? Theme.text : Theme.textMuted
                                    font.pixelSize: modelData.tipo === "testo" ? Theme.textSize : Theme.smallSize
                                    font.family: modelData.tipo === "risultato" ? Theme.monoFamily : Qt.application.font.family
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                    leftPadding: modelData.tipo === "risultato" ? 16 : 0
                                }
                            }
                            Section { title: "Che cosa ha risposto"; visible: (root.detail.response || "") !== "" && (root.detail.stream || []).length === 0 }
                            Mono { visible: (root.detail.response || "") !== "" && (root.detail.stream || []).length === 0; text: root.detail.response || "" }
                            Section { title: "Che cosa ha ricevuto"; visible: (root.detail.parts || []).length > 0 }
                            Repeater {
                                model: root.detail.parts || []
                                Column {
                                    required property var modelData
                                    width: detailColumn.width
                                    Mono { visible: parent.modelData.text !== undefined; text: parent.modelData.text || ""; maximumLineCount: partFull.checked ? 100000 : 40 }
                                    Image {
                                        visible: parent.modelData.image !== undefined
                                        source: parent.modelData.image ? "file://" + parent.modelData.image : ""
                                        width: Math.min(parent.width, 420)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        sourceSize.width: 840
                                    }
                                }
                            }
                            CheckBox { id: partFull; text: "Mostra tutto ciò che ha ricevuto"; visible: (root.detail.parts || []).some(p => (p.text || "").split("\n").length > 40) }
                            Section { title: "Le istruzioni"; visible: (root.detail.system || "") !== "" }
                            TxButton { visible: (root.detail.system || "") !== ""; text: systemText.visible ? "Nascondi" : "Mostra le istruzioni"; flat: true; onClicked: systemText.visible = !systemText.visible }
                            Mono { id: systemText; visible: false; text: root.detail.system || "" }
                        }
                    }
                }
            }
        }
        TxLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.mine
            horizontalAlignment: Text.AlignHCenter
            text: root.stateText
            color: Theme.textFaint
        }
    }

    component Section: TxLabel {
        property string title
        width: parent ? parent.width : 0
        text: title
        topPadding: Theme.gap / 2
        font.weight: Font.DemiBold
        color: Theme.textMuted
    }
    component Mono: Text {
        width: parent ? parent.width : 0
        color: Theme.text
        font.family: Theme.monoFamily
        font.pixelSize: Theme.smallSize
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        elide: Text.ElideRight
    }
}
