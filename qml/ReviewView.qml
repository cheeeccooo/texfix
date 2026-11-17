import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Rivedi dubbi e Rivedi correzioni, nella forma di Rivedi impaginazione (`ReviewDeck`): la panoramica lezione per
// lezione, con tutto (ciò che è deciso è oscurato, e si riapre), e la scheda con le scelte. Il riepilogo è la
// panoramica stessa.
Rectangle {
    id: root
    required property string section                   // dubbi | correzioni
    property var items: []
    signal lineAsked(string file, int line)
    color: Theme.window

    // Si rilegge tutto, ma una riga uguale a prima resta la stessa: la sua scheda non si rifà.
    function reload() {
        const fresh = Workspace.reviewItems(section, true), old = items
        for (let i = 0; i < fresh.length && i < old.length; ++i)
            if (JSON.stringify(fresh[i]) === JSON.stringify(old[i]))
                fresh[i] = old[i]
        items = fresh
    }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onFilesChanged() { if (root.visible) root.reload() } }
    Connections { target: Workspace; function onTreeChanged() { if (root.visible) root.reload() } }

    // Dall'albero: la panoramica (la lezione è lì).
    function showLesson(lesson) { deck.current = -1 }
    function showShots() { if (deck.cards.length > 0) deck.current = 0 }   // per le prove: la prima scheda

    readonly property bool doubts: section === "dubbi"
    function isOpen(row) {
        if (row.type === "doubt")
            return row.open === true
        if (row.type === "declared")
            return row.state === "aperta"
        return true                                    // un errore o una riga fuori misura è da sistemare
    }
    function badgeOf(row) {
        if (row.type === "doubt") {
            if (row.pending)
                return { text: "da confermare", color: Theme.warn }
            if (row.open)
                return { text: "da decidere", color: Theme.warn }
            return { text: ({ scelta: "scelta", confermata: "confermata", corretta: "corretta da te", modello: "deciso dal modello" })[row.how] || "decisa", color: Theme.good }
        }
        if (row.type === "declared")
            return ({ aperta: { text: "da decidere", color: Theme.warn }, tenuta: { text: "tenuta", color: Theme.good },
                      tolta: { text: "tolta", color: Theme.idle } })[row.state] || { text: row.state, color: Theme.idle }
        return row.kind === "error" ? { text: "errore", color: Theme.bad } : { text: row.kind === "wide" ? "riga troppo larga" : "riga troppo vuota", color: Theme.warn }
    }
    function nameOf(row) {
        if (row.type === "doubt")                     // la domanda si legge resa nella scheda: qui il suo numero
            return "Dubbio " + ((row.id || "").split("-d")[1] || "")
        if (row.type === "declared")
            return row.kind === "correction" ? "Correzione" : "Aggiunta"
        return (row.kind === "error" ? "Errore" : row.kind === "wide" ? "Riga troppo larga" : "Riga troppo vuota") + (row.line > 0 ? " · riga " + row.line : "")
    }

    ReviewDeck {
        id: deck
        anchors.fill: parent
        title: root.doubts ? "Rivedi dubbi" : "Rivedi correzioni"
        items: root.items
        emptyText: root.doubts ? "Nessun dubbio." : "Nessuna aggiunta o correzione, e niente da sistemare."
        isOpen: root.isOpen
        badgeOf: root.badgeOf
        nameOf: root.nameOf
        hintOf: row => row.type === "doubt" ? (row.source || "") + (row.page > 0 ? ", pagina " + row.page : "") : (row.text || "")
        onGoAsked: row => root.lineAsked(row.lesson, Math.max(1, row.line))
        tilePreview: preview
        stage: root.doubts ? doubtStage : correctionStage
        choices: root.doubts ? doubtChoices : correctionChoices
        lessonActions: lessonButtons
    }

    // ── i tasti
    function choose(index) {
        const c = deck.card
        if (!c || c.type !== "doubt" || c.pending || index >= (c.readings || []).length)
            return
        Workspace.chooseReading(c.lesson, c.id, index)
        deck.next()
    }
    Repeater {
        model: 9
        Item {
            required property int index
            Shortcut { sequence: "" + (index + 1); enabled: root.visible && root.doubts && deck.card !== null; onActivated: root.choose(index) }
        }
    }
    Shortcut {                                         // A: conferma un dubbio, tiene un'aggiunta (o la rimette)
        sequence: "A"
        enabled: root.visible && deck.card !== null
        onActivated: {
            const c = deck.card
            if (c.type === "doubt" && c.pending)
                Workspace.confirmDoubt(c.lesson, c.id)
            else if (c.type === "declared")
                c.state === "tolta" ? Workspace.restoreDeclared(c.lesson, c.hash) : Workspace.keepDeclared(c.lesson, c.hash, true)
            else
                return
            deck.next()
        }
    }
    Shortcut {                                         // L: toglie un'aggiunta
        sequence: "L"
        enabled: root.visible && deck.card !== null && deck.card.type === "declared" && deck.card.state !== "tolta"
        onActivated: { Workspace.dropDeclared(deck.card.lesson, deck.card.hash); deck.next() }
    }
    Shortcut {                                         // M: correggo io
        sequence: "M"
        enabled: root.visible && deck.card !== null && deck.card.type === "doubt" && deck.card.open && !deck.card.pending
        onActivated: { Workspace.correctDoubt(deck.card.lesson, deck.card.id); root.lineAsked(deck.card.lesson, Math.max(1, deck.card.line)) }
    }
    Shortcut {                                         // S: decide il modello; sistema errori e righe
        sequence: "S"
        enabled: root.visible && deck.card !== null && !Workspace.busy
        onActivated: {
            const c = deck.card
            if (c.type === "doubt" && c.open && !c.pending)
                Workspace.decideDoubts(c.lesson, [c.id])
            else if (c.type === "issue")
                Workspace.fixAll(c.lesson)
        }
    }
    Shortcut {                                         // R: riapre
        sequence: "R"
        enabled: root.visible && deck.card !== null && !root.isOpen(deck.card)
        onActivated: {
            const c = deck.card
            if (c.type === "doubt")
                Workspace.reopenDoubt(c.lesson, c.id, false)
            else if (c.type === "declared" && c.state === "tenuta")
                Workspace.keepDeclared(c.lesson, c.hash, false)
        }
    }

    // ── i pezzi
    Component {
        id: preview
        TxLabel {                                      // nella scheda piccola il testo: la resa si legge nella scheda aperta
            property var row: ({})
            text: row.type === "doubt" ? (row.what || "") + "\n\n" + (row.latex || "") : (row.text || row.detail || "")
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 9
            verticalAlignment: Text.AlignTop
        }
    }

    Component {
        id: lessonButtons
        RowLayout {
            property var row: ({})
            property var group: ({ cards: [] })
            spacing: Theme.gap
            readonly property int openDeclared: group.cards.filter(c => c.row.type === "declared" && c.row.state === "aperta").length
            readonly property bool anyDecided: group.cards.some(c => c.row.type !== "issue" && !root.isOpen(c.row))
            TxButton {
                visible: anyDecided
                text: "Riapri tutta"
                flat: true
                hint: "Tutto ciò che era deciso in questa lezione torna da decidere"
                onClicked: Workspace.reopenLesson(row.lesson, root.section)
            }
            TxButton {
                visible: root.doubts && row.open > 0
                text: "Fai decidere al modello tutti i dubbi"
                enabled: !Workspace.busy
                onClicked: Workspace.decideDoubts(row.lesson, [])
            }
            TxButton {
                visible: !root.doubts && openDeclared > 0
                text: "Tieni tutto"
                onClicked: Workspace.keepAllDeclared(row.lesson)
            }
            TxButton {
                visible: !root.doubts && group.cards.some(c => c.row.type === "issue")
                text: "Sistema errori e righe"
                enabled: !Workspace.busy
                onClicked: Workspace.fixAll(row.lesson)
            }
        }
    }

    // un dubbio: la domanda, il passo com'è scritto, la pagina della fonte; da confermare il prima e il dopo
    Component {
        id: doubtStage
        ColumnLayout {
            id: stageBox
            property var row: ({})
            property bool showSource: false
            spacing: Theme.gap
            TxRendered { text: row.what || ""; weight: Font.Medium; zoom: 0.62 }
            TxLabel { Layout.fillWidth: true; text: "Com'è scritto adesso"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
            TxRendered { text: row.latex || ""; kind: "latex"; zoom: 0.62 }
            ColumnLayout {
                Layout.fillWidth: true
                visible: row.pending === true
                spacing: 2
                TxLabel {
                    Layout.fillWidth: true
                    text: row.how === "modello" ? "Il modello: " + (row.why || "")
                          : (row.before || []).length + (row.after || []).length === 0 ? "Correggi il passo nella lezione e salva: qui compare il prima e il dopo." : "Corretto da te:"
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                Repeater {
                    model: row.before || []
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 6
                        TxLabel { text: "−"; color: Theme.bad; Layout.alignment: Qt.AlignTop }
                        TxRendered { text: parent.modelData; kind: "latex"; mono: true; textColor: Theme.bad }
                    }
                }
                Repeater {
                    model: row.after || []
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 6
                        TxLabel { text: "+"; color: Theme.good; Layout.alignment: Qt.AlignTop }
                        TxRendered { text: parent.modelData; kind: "latex"; mono: true; textColor: Theme.good }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TxLabel { Layout.fillWidth: true; text: (row.source || "") + (row.page > 0 ? ", pagina " + row.page : ""); color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                TxButton { text: stageBox.showSource ? "Nascondi il sorgente" : "Sorgente"; flat: true; onClicked: stageBox.showSource = !stageBox.showSource }
            }
            TxLabel {
                Layout.fillWidth: true
                visible: stageBox.showSource
                text: row.latex || ""
                font.family: Theme.monoFamily
                color: Theme.textMuted
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideNone
            }
            Image {                                    // la pagina della fonte
                Layout.fillWidth: true
                Layout.preferredHeight: status === Image.Ready ? width * sourceSize.height / Math.max(1, sourceSize.width) : 0
                visible: source != ""
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                asynchronous: true
                source: row.sourcePath ? "image://pdf/0/" + (Math.max(1, row.page) - 1) + "/1400/" + encodeURIComponent(row.sourcePath) : ""
            }
        }
    }

    Component {
        id: doubtChoices
        ColumnLayout {
            property var row: ({ readings: [] })
            spacing: Theme.gap
            // da confermare: corretto a mano o deciso dal modello
            TxChoice { visible: row.pending === true; text: "Conferma  (A)"; tone: Theme.good; onClicked: { Workspace.confirmDoubt(row.lesson, row.id); deck.next() } }
            TxChoice { visible: row.pending === true && row.how === "corretta"; text: "Modifica ancora"; tone: Theme.warn; onClicked: root.lineAsked(row.lesson, Math.max(1, row.line)) }
            TxChoice { visible: row.pending === true; text: "Riapri"; tone: Theme.idle; onClicked: Workspace.reopenDoubt(row.lesson, row.id, row.how === "modello") }
            // le letture, com'è nel PDF: la prima è com'è scritto
            TxLabel { visible: row.pending !== true; Layout.fillWidth: true; text: "Le letture"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
            Repeater {
                model: row.pending === true ? [] : (row.readings || [])
                TxChoice {
                    id: reading
                    required property var modelData
                    required property int index
                    tone: index === 0 ? Theme.idle : Theme.accent
                    chosen: !row.open && row.chosen === index
                    onClicked: root.choose(index)
                    RowLayout {
                        width: parent.width
                        spacing: 6
                        TxLabel { text: (reading.chosen ? "✓ " : "") + (reading.index + 1); color: Theme.textMuted; font.weight: Font.DemiBold }
                        TxRendered { Layout.fillWidth: true; text: reading.modelData.latex || ""; kind: "latex"; shown: reading.modelData.text }
                        TxLabel { visible: reading.index === 0; text: "com'è scritto"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                    }
                }
            }
            TxChoice {
                visible: row.open === true && row.pending !== true
                text: "Correggo io, nell'editor  (M)"
                tone: Theme.warn
                onClicked: { Workspace.correctDoubt(row.lesson, row.id); root.lineAsked(row.lesson, Math.max(1, row.line)) }
            }
            TxChoice {
                visible: row.open === true && row.pending !== true
                enabled: !Workspace.busy
                text: "Decidi tu  (S)"
                tone: Theme.accent
                onClicked: Workspace.decideDoubts(row.lesson, [row.id])
            }
            TxChoice { visible: row.open === false; text: "Riapri  (R)"; tone: Theme.idle; onClicked: Workspace.reopenDoubt(row.lesson, row.id, false) }
            TxChoice { visible: row.open === false && row.how === "scelta"; text: "Annulla la modifica"; tone: Theme.bad; onClicked: Workspace.reopenDoubt(row.lesson, row.id, true) }
            // dopo una decisione diversa da ciò che era scritto: ricordarla come regola del progetto
            RowLayout {
                Layout.fillWidth: true
                visible: row.open === false && row.how !== "confermata"
                spacing: Theme.gap
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.rowHeight
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: 1
                    border.color: rule.activeFocus ? Theme.accent : Theme.border
                    TextInput {
                        id: rule
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gap
                        anchors.rightMargin: Theme.gap
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.text
                        font.pixelSize: Theme.smallSize + 1
                        selectByMouse: true
                        clip: true
                        TxLabel { anchors.fill: parent; visible: parent.text === ""; text: "Ricorda come regola del progetto…"; color: Theme.textFaint; font.pixelSize: Theme.smallSize + 1 }
                    }
                }
                TxButton { text: "Ricorda"; enabled: rule.text.trim() !== ""; onClicked: { Workspace.addProjectRule(rule.text); rule.text = "" } }
            }
        }
    }

    // un'aggiunta o una correzione del modello, o un errore o una riga fuori misura
    Component {
        id: correctionStage
        ColumnLayout {
            id: stageBox
            property var row: ({})
            property bool showSource: false
            spacing: Theme.gap
            TxRendered {
                visible: row.type === "declared"
                text: row.text || ""
                kind: "latex"
                zoom: 0.62
                opacity: row.state === "tolta" ? 0.55 : 1
            }
            TxButton { visible: row.type === "declared"; text: stageBox.showSource ? "Nascondi il sorgente" : "Sorgente"; flat: true; onClicked: stageBox.showSource = !stageBox.showSource }
            TxLabel {
                Layout.fillWidth: true
                visible: stageBox.showSource || row.type === "issue"
                text: row.type === "issue" ? (row.text || row.detail || "") : (row.text || "")
                font.family: row.type === "issue" ? Qt.application.font.family : Theme.monoFamily
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
    }

    Component {
        id: correctionChoices
        ColumnLayout {
            property var row: ({})
            spacing: Theme.gap
            TxChoice {
                visible: row.type === "declared"
                text: row.state === "tolta" ? "Rimetti  (A)" : "Tieni  (A)"
                tone: Theme.good
                chosen: row.state === "tenuta"
                onClicked: {
                    row.state === "tolta" ? Workspace.restoreDeclared(row.lesson, row.hash) : Workspace.keepDeclared(row.lesson, row.hash, true)
                    deck.next()
                }
            }
            TxChoice {
                visible: row.type === "declared"
                enabled: row.state !== "tolta"
                text: "Togli  (L)"
                tone: Theme.bad
                chosen: row.state === "tolta"
                onClicked: { Workspace.dropDeclared(row.lesson, row.hash); deck.next() }
            }
            TxChoice { visible: row.type === "declared" && row.state === "tenuta"; text: "Riapri  (R)"; tone: Theme.idle; onClicked: Workspace.keepDeclared(row.lesson, row.hash, false) }
            TxChoice {
                visible: row.type === "issue"
                enabled: !Workspace.busy
                text: "Sistema errori e righe della lezione  (S)"
                tone: Theme.accent
                onClicked: Workspace.fixAll(row.lesson)
            }
        }
    }
}
