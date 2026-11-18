import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Il lavoro di «Avvia», a tutta finestra come le revisioni. In alto lo stato (in corso, in pausa per il limite di
// utilizzo, fermato, finito), quanto è fatto, il tempo, il costo, i tasti. A sinistra i cantieri: ogni lezione da scrivere
// con le sue fasi, ogni lezione del corso che l'agente completa. Al centro le chiamate ai sottoagenti, quelle in corso in
// cima; un clic apre a destra che cosa ha ricevuto, che cosa ha fatto e che cosa ha risposto. Alla fine, l'esito.
Rectangle {
    id: root
    color: Theme.window
    property string planId
    signal closeAsked()
    signal lineAsked(string file, int line)
    signal reviewAsked(string what)                    // «rivedi:dubbi»…

    readonly property var run: Workspace.run
    readonly property bool mine: run.id === planId
    readonly property string state: mine ? run.state : ""
    readonly property bool working: state === "in_corso" || state === "in_pausa"
    property string siteFilter: ""                     // un cantiere scelto: solo le sue chiamate
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
        const s = call.state === "in_corso" || call.state === "riprova" ? (now - call.started) / 1000 : call.seconds || 0
        return s < 60 ? Math.round(s) + " s" : Math.floor(s / 60) + " min " + Math.round(s % 60) + " s"
    }
    function stateColor(s) {
        return s === "fatta" ? Theme.good : s === "fallita" ? Theme.bad : s === "in_corso" || s === "riprova" ? Theme.accent
             : s === "limite" ? Theme.warn : s === "saltata" ? Theme.textFaint : Theme.idle
    }
    function callState(s) {
        return s === "in_corso" ? "in corso" : s === "riprova" ? "riprova" : s === "fatta" ? "fatta" : s === "fallita" ? "non riuscita"
             : s === "limite" ? "limite di utilizzo: aspetta" : s === "fermata" ? "fermata" : s === "interrotta" ? "interrotta" : s
    }
    function kindLabel(kind) {
        return kind === "converti" ? "Lezione nuova" : kind === "integra" ? "Aggiunte a una lezione del corso"
             : kind === "versione" ? "Versione nuova di una fonte" : kind === "fondi" ? "Lezione nuova, scritta dall'agente" : ""
    }
    readonly property string stateText: {
        if (!mine)
            return "Questo lavoro non c'è più: è stato archiviato o annullato."
        if (state === "in_corso")
            return "In corso"
        if (state === "in_pausa")
            return "In pausa: limite di utilizzo di Claude raggiunto. Riprende da solo alle " + run.until + "."
        const paused = run.until !== "" ? (run.untilPassed ? " Era in pausa per il limite di utilizzo: ora si può riprendere."
                                                            : " Era in pausa per il limite di utilizzo, fino alle " + run.until + ".") : ""
        if (state === "fermato")
            return "Fermato." + paused
        if (state === "interrotto")
            return "Interrotto dalla chiusura del programma." + paused
        return "Finito"
    }
    readonly property var calls: (run.calls || []).filter(c => siteFilter === "" || c.site === siteFilter)

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
                TxLabel { text: "Aggiungi materiale"; font.pixelSize: Theme.textSize + 3; font.weight: Font.DemiBold }
                Rectangle {
                    visible: root.mine
                    implicitWidth: stateLabel.implicitWidth + 14
                    implicitHeight: stateLabel.implicitHeight + 6
                    radius: 3
                    readonly property color tone: root.state === "finito" ? Theme.good : root.state === "in_pausa" ? Theme.warn
                                                : root.state === "in_corso" ? Theme.accent : Theme.idle
                    color: Qt.rgba(tone.r, tone.g, tone.b, 0.25)
                    Text {
                        id: stateLabel
                        anchors.centerIn: parent
                        text: root.state === "in_corso" ? "in corso" : root.state === "in_pausa" ? "in pausa" : root.state
                        color: Theme.text
                        font.pixelSize: Theme.smallSize
                        font.weight: Font.DemiBold
                    }
                }
                Item { Layout.fillWidth: true }
                TxButton { visible: root.state === "in_pausa"; text: "Riprendi ora"; hint: "Non aspetta l'ora detta dal limite (per esempio dopo aver cambiato account)"; onClicked: Workspace.wakeRun() }
                TxButton { visible: root.working; text: "Ferma"; hint: "Ciò che è fatto resta; si riprende da lì"; onClicked: Workspace.stop() }
                TxButton {
                    visible: root.state === "fermato" || root.state === "interrotto"
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
                TxButton { visible: root.state === "finito"; text: "Chiudi e archivia"; primary: true; hint: "L'esito va fra gli esiti; i registri delle chiamate si buttano"; onClicked: { Workspace.closeRun(); root.closeAsked() } }
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
                        width: parent.width * Math.min(1, root.state === "finito" ? 1 : root.run.progress || 0)
                        height: parent.height
                        radius: 4
                        color: root.state === "in_pausa" ? Theme.warn : root.state === "finito" ? Theme.good : Theme.accent
                    }
                }
                TxLabel { text: Math.round(100 * (root.state === "finito" ? 1 : root.run.progress || 0)) + "%"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
            }
            TxLabel {
                Layout.fillWidth: true
                visible: root.mine
                text: [root.stateText,
                       root.run.started > 0 ? "Tempo: " + root.minutes((root.run.ended > 0 && !root.working ? root.run.ended : root.now) - root.run.started) : "",
                       "Costo: " + root.money(root.run.cost) + (root.run.estimate > 0 ? " (stima " + root.money(root.run.estimate) + ")" : ""),
                       root.working ? "Sottoagenti al lavoro: " + root.run.active : ""].filter(s => s !== "").join("   ·   ")
                color: root.state === "in_pausa" ? Theme.warn : Theme.textMuted
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
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
                        visible: root.state === "finito"
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
                        text: "Chiamate ai sottoagenti  " + root.calls.length
                              + (root.siteFilter !== "" ? " · solo " + ((root.run.sites || []).find(s => s.key === root.siteFilter) || { title: "" }).title : "")
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }
                }
                ListView {
                    id: callList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.calls
                    spacing: 2
                    ScrollBar.vertical: ScrollBar { policy: callList.contentHeight > callList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                    delegate: Rectangle {
                        id: callRow
                        required property var modelData
                        readonly property bool open: root.chosen === modelData.id
                        x: 2 * Theme.gap
                        width: callList.width - 3 * Theme.gap
                        implicitHeight: callColumn.implicitHeight + 10
                        radius: Theme.radius
                        color: open ? Theme.selected : callArea.containsMouse ? Theme.hover : "transparent"
                        MouseArea {
                            id: callArea
                            anchors.fill: parent
                            hoverEnabled: true
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
                                    implicitWidth: 8; implicitHeight: 8; radius: 4
                                    color: root.stateColor(callRow.modelData.state)
                                    SequentialAnimation on opacity {
                                        running: callRow.modelData.state === "in_corso" && root.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 600 }
                                        NumberAnimation { to: 1; duration: 600 }
                                    }
                                }
                                TxLabel {
                                    text: callRow.modelData.role + (callRow.modelData.what ? " · " + callRow.modelData.what : "")
                                    font.weight: Font.Medium
                                }
                                TxLabel {
                                    Layout.fillWidth: true
                                    text: callRow.modelData.siteTitle || ""
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.smallSize
                                }
                                TxLabel {
                                    text: [callRow.modelData.model, root.seconds(callRow.modelData),
                                           callRow.modelData.cost > 0 ? root.money(callRow.modelData.cost) : ""].filter(s => s !== "").join(" · ")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.smallSize
                                }
                            }
                            TxLabel {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: callRow.modelData.state === "limite" ? (root.state === "in_pausa" ? "limite di utilizzo raggiunto: si rifà alla ripresa, alle " + root.run.until : "limite di utilizzo raggiunto: si rifà alla ripresa")
                                      : callRow.modelData.state === "fallita" || callRow.modelData.state === "riprova"
                                      ? root.callState(callRow.modelData.state) + (callRow.modelData.error ? ": " + callRow.modelData.error : "")
                                      : callRow.modelData.state === "in_corso" ? (callRow.modelData.last || "")
                                      : callRow.modelData.state === "fermata" || callRow.modelData.state === "interrotta" ? root.callState(callRow.modelData.state) : ""
                                color: callRow.modelData.state === "fallita" ? Theme.bad : callRow.modelData.state === "in_corso" ? Theme.textMuted : Theme.warn
                                font.pixelSize: Theme.smallSize
                                leftPadding: 14
                            }
                        }
                    }
                }
                TxLabel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.calls.length === 0
                    horizontalAlignment: Text.AlignHCenter
                    text: root.working ? "Le chiamate compaiono qui appena partono." : "Nessuna chiamata."
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
