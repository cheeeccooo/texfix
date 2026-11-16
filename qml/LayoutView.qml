import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Rivedi impaginazione. La panoramica: lezione per lezione, una scheda piccola per mossa, in ordine di documento.
// Un clic apre la scheda: a sinistra le pagine com'erano e come sono venute, a destra che cosa è successo e le scelte
// (Accetta, all'agente, a mano, com'era), con i tasti A, S, M, L, le frecce e Esc.
Rectangle {
    id: root
    color: Theme.window
    signal lineAsked(string file, int line)
    signal manualAsked(string lesson, string block, int line, string what)

    property var items: []
    property int current: -1                           // la scheda aperta (fra le mosse); -1: la panoramica
    readonly property var moves: items.filter(i => i.type === "move")
    readonly property var card: current >= 0 && current < moves.length ? moves[current] : null
    // le lezioni con le loro mosse: lesson, title, todo, fresh, cards (con index fra le mosse)
    readonly property var groups: {
        const out = []
        let n = 0
        for (const item of items) {
            if (item.type === "lesson")
                out.push({ lesson: item.lesson, title: item.title, todo: false, fresh: false, cards: [] })
            else if (item.type === "todo") {
                out[out.length - 1].todo = true
                out[out.length - 1].fresh = item.fresh
            } else if (out.length > 0)
                out[out.length - 1].cards.push({ index: n++, row: item })
        }
        return out
    }

    function reload() {
        items = Workspace.layoutItems()
        if (current >= moves.length)
            current = -1
    }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onFilesChanged() { if (root.visible) root.reload() } }
    Connections { target: Workspace; function onTreeChanged() { if (root.visible) root.reload() } }

    function showLesson(lesson) { current = -1 }       // dall'albero: la panoramica
    function showShots() { if (moves.length > 0) current = 0 }   // per le prove: la prima scheda

    readonly property var openStates: ["fatta", "proposta", "segnalata", "a_mano", "agente"]
    function isOpen(row) { return openStates.includes(row.state) }
    // il programma ha fatto o proposto una mossa (c'è qualcosa da lasciare com'era); altrimenti ha solo segnalato
    function hasMove(row) { return row.origin === "fatta" || row.origin === "proposta" }
    function plain(shot) { return { path: shot.path, rects: [] } }
    // Il nome del blocco: l'inizio di ciò che è successo.
    function nameOf(what) {
        const m = /^(.*?)(?= cadeva| cade | non ci stava| finiva| è più lungo|: | resta | lascia )/.exec(what || "")
        return m ? m[1] : (what || "").split(".")[0]
    }
    function badgeOf(row) {
        if (row.role === "attesa")
            return { text: "aspetta le mosse prima", color: Theme.idle }
        return ({ fatta: { text: "da rivedere", color: Theme.warn }, proposta: { text: "da rivedere", color: Theme.warn },
                  segnalata: { text: "da rivedere", color: Theme.warn }, a_mano: { text: "a mano", color: Theme.accent },
                  agente: { text: "all'agente", color: Theme.accent }, accettata: { text: "accettata", color: Theme.good },
                  risolta: { text: "risolta a mano", color: Theme.good } })[row.state] || { text: "lasciata com'era", color: Theme.idle }
    }
    function act(state) {
        if (!card || card.locked)
            return
        const at = current
        Workspace.decideMove(card.lesson, card.block, state)
        // alla prossima scheda ancora da decidere, se c'è
        for (let i = at + 1; i < moves.length; ++i)
            if (isOpen(moves[i]))
                return current = i
    }
    function manual() {
        if (card && !card.locked)
            root.manualAsked(card.lesson, card.block, card.line, card.what)
    }
    function step(by) { if (card) current = Math.max(0, Math.min(moves.length - 1, current + by)) }

    Shortcut { sequence: "Escape"; enabled: root.visible && root.card !== null; onActivated: root.current = -1 }
    Shortcut { sequence: "Left"; enabled: root.visible && root.card !== null; onActivated: root.step(-1) }
    Shortcut { sequence: "Right"; enabled: root.visible && root.card !== null; onActivated: root.step(1) }
    Shortcut { sequence: "A"; enabled: root.visible && root.card !== null; onActivated: root.act("accettata") }
    Shortcut { sequence: "S"; enabled: root.visible && root.card !== null; onActivated: if (root.isOpen(root.card)) root.act("agente") }
    Shortcut { sequence: "M"; enabled: root.visible && root.card !== null; onActivated: root.manual() }
    Shortcut { sequence: "L"; enabled: root.visible && root.card !== null; onActivated: if (root.hasMove(root.card)) root.act("lasciata") }

    // una pagina in piccolo, con il contorno del blocco
    component Page: Rectangle {
        property var shot: ({ path: "", rects: [] })
        property color tone: Theme.good
        color: "white"
        border.width: 1
        border.color: Theme.border
        Image {
            anchors.fill: parent
            anchors.margins: 1
            source: parent.shot.path ? "file://" + parent.shot.path : ""
            cache: false
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
        Repeater {
            model: parent.shot.rects || []
            Rectangle {
                required property var modelData
                x: modelData.x * parent.width
                y: modelData.y * parent.height
                width: modelData.w * parent.width
                height: modelData.h * parent.height
                color: Qt.rgba(parent.tone.r, parent.tone.g, parent.tone.b, 0.10)
                border.width: 2
                border.color: parent.tone
            }
        }
    }
    // un tasto con il suo colore
    component Choice: Rectangle {
        id: choice
        property string text
        property color tone: Theme.surface
        property bool chosen: false
        signal clicked()
        Layout.fillWidth: true
        implicitHeight: Theme.rowHeight + 8
        radius: Theme.radius
        opacity: enabled ? 1 : 0.35
        color: Qt.rgba(tone.r, tone.g, tone.b, choiceArea.pressed ? 0.45 : choiceArea.containsMouse ? 0.34 : chosen ? 0.4 : 0.22)
        border.width: chosen ? 2 : 1
        border.color: tone
        TxLabel { anchors.centerIn: parent; width: parent.width - 2 * Theme.gap; horizontalAlignment: Text.AlignHCenter; text: (choice.chosen ? "✓ " : "") + choice.text }
        MouseArea { id: choiceArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: choice.clicked() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // in alto: dove si è, e ciò che vale per tutto
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            Layout.leftMargin: 2 * Theme.gap + 4
            spacing: Theme.gap
            TxButton { visible: root.card !== null; text: "←  Panoramica"; hint: "Tutte le schede (Esc)"; onClicked: root.current = -1 }
            TxLabel {
                Layout.fillWidth: true
                text: root.card ? "Scheda " + (root.current + 1) + " di " + root.moves.length : "Rivedi impaginazione"
                font.pixelSize: Theme.textSize + 3
                font.weight: Font.DemiBold
            }
            TxButton {
                id: wipe
                visible: root.card === null
                property bool armed: false             // chiede conferma: un secondo clic
                text: armed ? "Sì, elimina tutte le impaginazioni" : "Elimina le impaginazioni"
                primary: armed
                hint: "Toglie tutte le mosse dalle lezioni non fatte, per farle rifare. ⌘Z lo annulla."
                enabled: !Workspace.busy
                onClicked: {
                    if (!armed)
                        return armed = true
                    armed = false
                    Workspace.resetLayout()
                }
                Timer { interval: 5000; running: wipe.armed; onTriggered: wipe.armed = false }
            }
            TxButton { visible: root.card === null; text: "Impagina ciò che è nuovo"; enabled: !Workspace.busy; onClicked: Workspace.paginate() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // la panoramica
        TxScroll {
            id: overview
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.card === null
            contentHeight: grid.implicitHeight + 3 * Theme.gap
            Column {
                id: grid
                x: 2 * Theme.gap + 4
                y: Theme.gap
                width: overview.width - 4 * Theme.gap - 8
                spacing: 2 * Theme.gap
                Repeater {
                    model: root.groups
                    Column {
                        id: lessonGroup
                        required property var modelData
                        readonly property int pending: modelData.cards.filter(c => root.isOpen(c.row)).length
                        readonly property bool canRest: modelData.cards.some(c => c.row.state === "fatta")
                        width: grid.width
                        spacing: Theme.gap
                        RowLayout {
                            width: parent.width
                            spacing: Theme.gap
                            TxLabel { text: lessonGroup.modelData.title; font.weight: Font.DemiBold; font.pixelSize: Theme.textSize + 1 }
                            TxLabel {
                                Layout.fillWidth: true
                                text: lessonGroup.modelData.cards.length === 0 ? ""
                                      : lessonGroup.modelData.cards.length + (lessonGroup.modelData.cards.length === 1 ? " mossa" : " mosse") + (lessonGroup.pending > 0 ? " · " + lessonGroup.pending + " da rivedere" : " · tutte decise")
                                color: lessonGroup.pending > 0 ? Theme.warn : Theme.textFaint
                                font.pixelSize: Theme.smallSize
                            }
                            TxButton {
                                visible: lessonGroup.canRest
                                text: "Accetta le restanti"
                                hint: "Accetta tutte le mosse fatte da sole in questa lezione"
                                onClicked: Workspace.acceptRest(lessonGroup.modelData.lesson)
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                        TxLabel {
                            visible: lessonGroup.modelData.todo
                            width: parent.width
                            text: lessonGroup.modelData.fresh ? "Non ancora impaginata: «Impagina ciò che è nuovo»." : "Cambiata dopo l'ultima impaginazione: «Impagina ciò che è nuovo» la rifà."
                            color: Theme.warn
                        }
                        Flow {
                            width: parent.width
                            spacing: Theme.gap + 2
                            Repeater {
                                model: lessonGroup.modelData.cards
                                Rectangle {
                                    id: tile
                                    required property var modelData
                                    readonly property var row: modelData.row
                                    readonly property var badge: root.badgeOf(row)
                                    readonly property bool now: row.role === "attuale"
                                    readonly property var shot: (row.after || []).length > 0 ? row.after[0] : (row.before || []).length > 0 ? (root.hasMove(row) ? row.before[0] : root.plain(row.before[0])) : null
                                    width: 232
                                    height: 268
                                    radius: Theme.radius
                                    color: tileArea.containsMouse ? Theme.hover : Theme.surface
                                    border.width: now ? 2 : 1
                                    border.color: now ? badge.color : Theme.border
                                    opacity: now ? 1 : 0.5              // quelle già decise e quelle che aspettano sono oscurate
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: Theme.gap
                                        spacing: 6
                                        Row {
                                            spacing: 4
                                            Rectangle {
                                                width: tag.implicitWidth + 12
                                                height: tag.implicitHeight + 4
                                                radius: 3
                                                color: Qt.rgba(tile.badge.color.r, tile.badge.color.g, tile.badge.color.b, 0.25)
                                                Text { id: tag; anchors.centerIn: parent; text: tile.badge.text; color: Theme.text; font.pixelSize: Theme.smallSize - 1; font.weight: Font.DemiBold }
                                            }
                                            Rectangle {
                                                visible: tile.row.same === true
                                                width: sameMark.implicitWidth + 12
                                                height: sameMark.implicitHeight + 4
                                                radius: 3
                                                color: "transparent"
                                                border.width: 1
                                                border.color: Theme.border
                                                Text { id: sameMark; anchors.centerIn: parent; text: "invariata"; color: Theme.textMuted; font.pixelSize: Theme.smallSize - 1; font.weight: Font.DemiBold }
                                            }
                                        }
                                        TxLabel {
                                            width: parent.width
                                            height: 2 * (Theme.textSize + 5)
                                            text: root.nameOf(tile.row.what)
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 2
                                            font.weight: Font.Medium
                                        }
                                        Page {
                                            visible: tile.shot !== null
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            height: 168
                                            width: height / 1.414
                                            shot: tile.shot || ({ path: "", rects: [] })
                                            tone: (tile.row.after || []).length > 0 ? Theme.good : Theme.bad
                                        }
                                    }
                                    MouseArea { id: tileArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.current = tile.modelData.index }
                                    // annulla la scelta di prima e torna lì (a ritroso, una alla volta)
                                    Rectangle {
                                        visible: tile.row.undo !== "" && (tileArea.containsMouse || undoArea.containsMouse) && !Workspace.busy
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 6
                                        width: 22; height: 22; radius: 11
                                        color: undoArea.containsMouse ? Theme.bad : Theme.border
                                        Text { anchors.centerIn: parent; text: "×"; color: Theme.text; font.pixelSize: 15 }
                                        MouseArea {
                                            id: undoArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: Workspace.reopenMove(tile.row.lesson, tile.row.undo)
                                        }
                                        ToolTip.visible: undoArea.containsMouse
                                        ToolTip.delay: 400
                                        ToolTip.text: tile.row.undo === tile.row.block ? "Annulla questa scelta: la mossa torna da rivedere" : "Annulla la scelta di prima e torna a quella mossa"
                                    }
                                    ToolTip.visible: tileArea.containsMouse
                                    ToolTip.delay: 700
                                    ToolTip.text: tile.row.what
                                }
                            }
                        }
                    }
                }
            }
        }
        TxLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.card === null && root.items.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: "Niente da impaginare e nessuna mossa da rivedere."
            color: Theme.textFaint
        }

        // una scheda: prima e dopo, e le scelte
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.gap + 4
            Layout.leftMargin: 2 * Theme.gap + 4
            visible: root.card !== null
            spacing: 2 * Theme.gap
            Item {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property var before: root.card ? (root.hasMove(root.card) ? (root.card.before || []) : (root.card.before || []).map(root.plain)) : []
                readonly property var after: root.card ? (root.card.after || []) : []
                readonly property int count: Math.max(1, before.length + after.length)
                readonly property real pageWidth: Math.max(60, Math.min((width - (count + 1) * 8 - 24) / count, (height - 30) / 1.414))
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 24
                    Repeater {
                        model: [{ title: "Com'era", pages: stage.before, tone: Theme.bad }, { title: "Com'è venuta", pages: stage.after, tone: Theme.good }].filter(g => g.pages.length > 0)
                        Column {
                            id: side
                            required property var modelData
                            spacing: 6
                            TxLabel { text: side.modelData.title; color: side.modelData.tone; font.weight: Font.DemiBold; font.pixelSize: Theme.smallSize; font.capitalization: Font.AllUppercase }
                            Row {
                                spacing: 8
                                Repeater {
                                    model: side.modelData.pages
                                    Page {
                                        required property var modelData
                                        width: stage.pageWidth
                                        height: width * 1.414
                                        shot: modelData
                                        tone: side.modelData.tone
                                    }
                                }
                            }
                        }
                    }
                }
                TxLabel {
                    anchors.centerIn: parent
                    visible: stage.before.length + stage.after.length === 0
                    text: "Di questa mossa non ci sono le pagine: si rifanno alla prossima impaginazione della lezione."
                    color: Theme.textFaint
                }
            }
            // le scelte
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                spacing: Theme.gap
                Row {
                    spacing: 6
                    Rectangle {
                        readonly property var badge: root.card ? root.badgeOf(root.card) : ({ text: "", color: Theme.idle })
                        width: state.implicitWidth + 14
                        height: state.implicitHeight + 6
                        radius: 3
                        color: Qt.rgba(badge.color.r, badge.color.g, badge.color.b, 0.28)
                        Text { id: state; anchors.centerIn: parent; text: parent.badge.text; color: Theme.text; font.pixelSize: Theme.smallSize; font.weight: Font.DemiBold }
                    }
                    Rectangle {                        // la mossa non ha cambiato ciò che si vede
                        visible: root.card !== null && root.card.same === true
                        width: sameTag.implicitWidth + 14
                        height: sameTag.implicitHeight + 6
                        radius: 3
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.border
                        Text { id: sameTag; anchors.centerIn: parent; text: "pagina invariata"; color: Theme.textMuted; font.pixelSize: Theme.smallSize; font.weight: Font.DemiBold }
                    }
                }
                TxLabel { Layout.fillWidth: true; text: root.card ? root.nameOf(root.card.what) : ""; font.weight: Font.DemiBold; font.pixelSize: Theme.textSize + 2; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                TxLabel {
                    Layout.fillWidth: true
                    text: root.card ? (root.groups.find(g => g.cards.some(c => c.index === root.current)) || { title: "" }).title : ""
                    color: Theme.textFaint
                    font.pixelSize: Theme.smallSize
                }
                TxLabel { Layout.fillWidth: true; text: root.card ? root.card.what : ""; color: Theme.textMuted; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                TxLabel {
                    Layout.fillWidth: true
                    visible: root.card !== null && root.card.locked === true
                    text: root.card && root.card.role === "chiusa"
                          ? "Già decisa. Per cambiarla, dalla panoramica annulla le scelte a ritroso (la × sulla scheda attuale): l'ordine conta."
                          : "Prima rivedi le mosse che vengono prima in questa lezione: l'ordine conta, cambiandone una cambia tutto ciò che segue."
                    color: Theme.warn
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                Item { implicitHeight: Theme.gap }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    enabled: root.card !== null && root.card.locked !== true
                    Choice {
                        visible: root.card !== null && root.card.state !== "a_mano" && root.card.state !== "agente"
                        text: root.card && root.card.state === "proposta" ? "Accetta: a pagina nuova  (A)" : "Accetta  (A)"
                        tone: Theme.good
                        chosen: root.card !== null && root.card.state === "accettata"
                        onClicked: root.act("accettata")
                    }
                    Choice {
                        visible: root.card !== null && (root.card.state === "a_mano" || root.card.state === "agente")
                        text: "Risolta"
                        tone: Theme.good
                        onClicked: root.act("risolta")
                    }
                    Choice {
                        visible: root.card !== null && root.isOpen(root.card)
                        text: "Manda all'agente  (S)"
                        tone: Theme.accent
                        chosen: root.card !== null && root.card.state === "agente"
                        onClicked: root.act("agente")
                    }
                    Choice {
                        text: "Risolvo a mano, nell'editor  (M)"
                        tone: Theme.warn
                        chosen: root.card !== null && root.card.state === "a_mano"
                        onClicked: root.manual()
                    }
                    Choice {
                        visible: root.card !== null && root.hasMove(root.card)      // se il programma non ha fatto niente non c'è niente da lasciare
                        text: "Lascia com'era  (L)"
                        tone: Theme.idle
                        chosen: root.card !== null && root.card.state === "lasciata"
                        onClicked: root.act("lasciata")
                    }
                }
                TxLabel {
                    Layout.fillWidth: true
                    text: "Cambiare una mossa fa ricalcolare quelle non ancora decise che seguono."
                    color: Theme.textFaint
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                RowLayout {
                    Layout.fillWidth: true
                    TxButton { text: "←"; hint: "La scheda prima"; enabled: root.current > 0; onClicked: root.step(-1) }
                    Item { Layout.fillWidth: true }
                    TxButton { visible: root.card !== null && root.card.line > 0; text: "Apri alla riga"; flat: true; onClicked: root.lineAsked(root.card.lesson, root.card.line) }
                    Item { Layout.fillWidth: true }
                    TxButton { text: "→"; hint: "La scheda dopo"; enabled: root.current < root.moves.length - 1; onClicked: root.step(1) }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
