import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// La forma comune di Rivedi dubbi, correzioni e figure. All'inizio l'elenco delle lezioni, ognuna con quanto resta da
// rivedere; un clic apre la lezione, con tutti i suoi elementi uno sotto l'altro, interi: a sinistra ciò che c'è da
// guardare, a destra le scelte (ciò che è deciso è oscurato). Ciò che si vede si ricorda per nome (la lezione) e per
// chiave (l'elemento evidenziato), mai per posizione: quando le righe si rileggono (una decisione, una compilazione
// che finisce) si resta dove si era. Chi la usa dà le righe (`items`: type «lesson» e le altre) e i pezzi: la scena e
// le scelte di un elemento, i tasti di una lezione e quelli in alto.
Rectangle {
    id: deck
    color: Theme.window
    property string title
    property var items: []
    property string emptyText: "Niente da rivedere."
    property var isOpen: row => true                   // da rivedere: in vista; deciso: oscurato
    property var badgeOf: row => ({ text: "", color: Theme.idle })
    property var nameOf: row => ""
    property var hintOf: row => ""
    property var keyOf: row => JSON.stringify([row.type, row.lesson, row.id || row.hash || ((row.kind || "") + ":" + (row.line || 0) + ":" + (row.text || ""))])
    property string goLabel: "Apri alla riga"
    property int choicesWidth: 320
    property Component stage                           // a sinistra in un elemento (row)
    property Component choices                         // a destra in un elemento (row)
    property Component lessonActions                   // i tasti di una lezione (row: la lezione; group)
    default property alias actions: actionRow.data     // i tasti in alto, nell'elenco delle lezioni
    signal goAsked(var row)

    // le lezioni con i loro elementi: row (la riga della lezione), cards (le righe)
    readonly property var groups: {
        const out = []
        for (const item of items) {
            if (item.type === "lesson")
                out.push({ row: item, cards: [] })
            else if (out.length > 0)
                out[out.length - 1].cards.push(item)
        }
        return out
    }
    property string lesson: ""                         // la lezione aperta; vuoto: l'elenco
    readonly property var group: lesson !== "" ? groups.find(g => g.row.lesson === lesson) || null : null
    readonly property var cards: group ? group.cards : []
    property string focusKey: ""                       // l'elemento evidenziato: i tasti valgono per lui
    readonly property var card: cards.find(c => keyOf(c) === focusKey) || null
    function isFocused(row) { return focusKey !== "" && keyOf(row) === focusKey }
    function pendingOf(g) { return g.cards.filter(c => isOpen(c)).length }

    // una lezione che non ha più niente da mostrare riporta all'elenco
    onGroupChanged: if (lesson !== "" && group === null) Qt.callLater(() => { if (deck.group === null) deck.back() })

    property bool listShown: true                      // gli elementi si rifanno passando a un'altra lezione
    function open(name) {
        const g = groups.find(x => x.row.lesson === name)
        if (!g)
            return lesson = ""
        listShown = false
        lesson = name
        const first = g.cards.find(c => isOpen(c)) || g.cards[0]
        focusKey = first ? keyOf(first) : ""
        list.contentY = 0
        Qt.callLater(() => deck.listShown = true)
    }
    function back() { lesson = ""; focusKey = "" }
    function stepLesson(by) {
        const at = groups.findIndex(g => g.row.lesson === lesson)
        if (at >= 0 && at + by >= 0 && at + by < groups.length)
            open(groups[at + by].row.lesson)
    }
    // un elemento prima o dopo, e lo si porta in vista
    function step(by) {
        if (cards.length === 0)
            return
        const at = cards.findIndex(c => keyOf(c) === focusKey)
        const to = at < 0 ? 0 : Math.max(0, Math.min(cards.length - 1, at + by))
        focusKey = keyOf(cards[to])
        Qt.callLater(() => deck.reveal(to))
    }
    // dopo una decisione su `row`: al prossimo elemento ancora da rivedere della lezione, se c'è
    function next(row) {
        const from = cards.findIndex(c => keyOf(c) === keyOf(row || card || {}))
        for (let i = from + 1; i < cards.length; ++i)
            if (isOpen(cards[i])) {
                focusKey = keyOf(cards[i])
                return Qt.callLater(() => deck.reveal(i))
            }
    }
    function reveal(index) {
        const entry = entries.itemAt(index)
        if (!entry)
            return
        const top = entryColumn.y + entry.y, bottom = top + entry.height
        if (top < list.contentY)
            list.contentY = Math.max(0, top - Theme.gap)
        else if (bottom > list.contentY + list.height)
            list.contentY = Math.max(0, Math.min(top - Theme.gap, list.contentHeight - list.height))
    }

    Shortcut { sequence: "Escape"; enabled: deck.visible && deck.lesson !== ""; onActivated: deck.back() }
    Shortcut { sequence: "Up"; enabled: deck.visible && deck.lesson !== ""; onActivated: deck.step(-1) }
    Shortcut { sequence: "Down"; enabled: deck.visible && deck.lesson !== ""; onActivated: deck.step(1) }
    Shortcut { sequence: "Left"; enabled: deck.visible && deck.lesson !== ""; onActivated: deck.stepLesson(-1) }
    Shortcut { sequence: "Right"; enabled: deck.visible && deck.lesson !== ""; onActivated: deck.stepLesson(1) }

    // un pezzo dato da chi usa la vista, con la sua riga
    component Slot: Loader {
        property var row: null
        property var group: null
        onLoaded: { item.row = row; if ("group" in item) item.group = group }
        onRowChanged: if (item) item.row = row
        onGroupChanged: if (item && "group" in item) item.group = group
    }
    component Badge: Rectangle {
        property var badge: ({ text: "", color: Theme.idle })
        implicitWidth: badgeText.implicitWidth + 12
        implicitHeight: badgeText.implicitHeight + 4
        radius: 3
        visible: badge.text !== ""
        color: Qt.rgba(badge.color.r, badge.color.g, badge.color.b, 0.25)
        Text { id: badgeText; anchors.centerIn: parent; text: parent.badge.text; color: Theme.text; font.pixelSize: Theme.smallSize - 1; font.weight: Font.DemiBold }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // in alto: dove si è, e ciò che vale per tutto (nell'elenco) o per la lezione (dentro)
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            Layout.leftMargin: 2 * Theme.gap + 4
            spacing: Theme.gap
            TxButton { visible: deck.group !== null; text: "←  Lezioni"; hint: "Tutte le lezioni (Esc)"; onClicked: deck.back() }
            TxLabel {
                Layout.fillWidth: true
                text: deck.group ? deck.group.row.title || "" : deck.title
                font.pixelSize: Theme.textSize + 3
                font.weight: Font.DemiBold
            }
            TxLabel {
                visible: deck.group !== null
                text: deck.group ? (deck.pendingOf(deck.group) > 0 ? deck.pendingOf(deck.group) + " da rivedere su " + deck.cards.length : "tutto deciso") : ""
                color: deck.group && deck.pendingOf(deck.group) > 0 ? Theme.warn : Theme.textFaint
                font.pixelSize: Theme.smallSize
            }
            Slot { visible: deck.group !== null; sourceComponent: deck.group ? deck.lessonActions : null; row: deck.group ? deck.group.row : ({}); group: deck.group || ({ row: {}, cards: [] }) }
            TxButton { visible: deck.group !== null; text: "‹"; hint: "La lezione prima (←)"; enabled: deck.group !== null && deck.groups.indexOf(deck.group) > 0; onClicked: deck.stepLesson(-1) }
            TxButton { visible: deck.group !== null; text: "›"; hint: "La lezione dopo (→)"; enabled: deck.group !== null && deck.groups.indexOf(deck.group) < deck.groups.length - 1; onClicked: deck.stepLesson(1) }
            RowLayout { id: actionRow; visible: deck.group === null; spacing: Theme.gap }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // l'elenco delle lezioni
        TxScroll {
            id: lessonList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: deck.group === null && deck.groups.length > 0
            contentHeight: lessonColumn.implicitHeight + 2 * Theme.gap
            Column {
                id: lessonColumn
                x: 2 * Theme.gap + 4
                y: Theme.gap
                width: Math.min(lessonList.width - 4 * Theme.gap - 8, 860)
                spacing: 4
                Repeater {
                    model: deck.groups
                    Rectangle {
                        id: lessonRow
                        required property var modelData
                        readonly property int pending: deck.pendingOf(modelData)
                        width: lessonColumn.width
                        height: 2 * Theme.rowHeight + 4
                        radius: Theme.radius
                        color: rowArea.containsMouse ? Theme.hover : Theme.surface
                        border.width: 1
                        border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.gap + 4
                            anchors.rightMargin: Theme.gap + 4
                            spacing: Theme.gap
                            Rectangle {
                                implicitWidth: 9
                                implicitHeight: 9
                                radius: 4.5
                                color: lessonRow.pending > 0 ? Theme.warn : Theme.good
                            }
                            TxLabel {
                                Layout.fillWidth: true
                                text: lessonRow.modelData.row.title || ""
                                font.weight: Font.Medium
                            }
                            TxLabel {
                                text: (lessonRow.pending > 0 ? lessonRow.pending + " da rivedere su " + lessonRow.modelData.cards.length : "tutto deciso, " + lessonRow.modelData.cards.length)
                                      + (lessonRow.modelData.row.done === true ? " · lezione fatta" : "")
                                color: lessonRow.pending > 0 ? Theme.warn : Theme.textFaint
                                font.pixelSize: Theme.smallSize
                            }
                            TxLabel { text: "›"; color: Theme.textMuted; font.pixelSize: Theme.textSize + 4 }
                        }
                        MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: deck.open(lessonRow.modelData.row.lesson) }
                    }
                }
            }
        }
        TxLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: deck.group === null && deck.groups.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: deck.emptyText
            color: Theme.textFaint
        }

        // una lezione: tutti i suoi elementi, interi
        TxScroll {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: deck.group !== null
            contentHeight: entryColumn.implicitHeight + 3 * Theme.gap
            Column {
                id: entryColumn
                x: 2 * Theme.gap + 4
                y: Theme.gap
                width: list.width - 4 * Theme.gap - 8
                spacing: Theme.gap
                Repeater {
                    id: entries
                    // per numero: le righe che si rileggono aggiornano gli elementi che ci sono, non li rifanno
                    model: deck.listShown ? deck.cards.length : 0
                    Rectangle {
                        id: entry
                        required property int index
                        readonly property var row: deck.cards[index] || ({})
                        readonly property bool open: deck.isOpen(row)
                        readonly property bool focused: deck.isFocused(row)
                        readonly property var badge: deck.badgeOf(row)
                        readonly property bool wide: width > 860
                        width: entryColumn.width
                        height: body.implicitHeight + 2 * Theme.gap + 4
                        radius: Theme.radius
                        color: Theme.surface
                        border.width: focused ? 2 : 1
                        border.color: focused ? Theme.accent : open ? Qt.rgba(badge.color.r, badge.color.g, badge.color.b, 0.7) : Theme.border
                        // un clic dove non c'è un tasto evidenzia l'elemento
                        MouseArea { anchors.fill: parent; onPressed: mouse => { deck.focusKey = deck.keyOf(entry.row); mouse.accepted = false } }
                        GridLayout {
                            id: body
                            x: Theme.gap + 4
                            y: Theme.gap + 2
                            width: parent.width - 2 * Theme.gap - 8
                            columns: entry.wide ? 2 : 1
                            columnSpacing: 2 * Theme.gap
                            rowSpacing: Theme.gap
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: Theme.gap
                                opacity: entry.open ? 1 : 0.55            // ciò che è deciso è oscurato
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.gap
                                    Badge { badge: entry.badge }
                                    TxLabel { Layout.fillWidth: true; text: deck.nameOf(entry.row); font.weight: Font.DemiBold }
                                    TxButton { visible: (entry.row.line || 0) > 0; text: deck.goLabel; flat: true; hint: deck.hintOf(entry.row); onClicked: deck.goAsked(entry.row) }
                                }
                                Slot {
                                    Layout.fillWidth: true
                                    sourceComponent: deck.stage
                                    row: entry.row
                                }
                            }
                            Slot {
                                Layout.fillWidth: !entry.wide
                                Layout.preferredWidth: entry.wide ? deck.choicesWidth : -1
                                Layout.maximumWidth: entry.wide ? deck.choicesWidth : Number.POSITIVE_INFINITY
                                Layout.alignment: Qt.AlignTop
                                sourceComponent: deck.choices
                                row: entry.row
                            }
                        }
                    }
                }
            }
        }
    }
}
