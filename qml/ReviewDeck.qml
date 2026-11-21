import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// La forma comune di Rivedi dubbi, correzioni e figure. All'inizio l'elenco delle lezioni, ognuna con quanto resta da
// rivedere; un clic apre la lezione, con tutti i suoi elementi uno sotto l'altro, interi: a sinistra ciò che c'è da
// guardare, a destra le scelte (ciò che è deciso è oscurato). Chi la usa dà le righe (`items`: type «lesson» e le
// altre) e i pezzi: la scena e le scelte di un elemento, i tasti di una lezione e quelli in alto.
//
// Niente si sposta da sé. La lezione aperta si ricorda per nome e resta aperta anche se per un momento non ha più
// niente (una compilazione che cambia gli errori); gli elementi si tengono per chiave (`entries`, aggiornato con il
// minimo: un elemento che c'era resta lo stesso, non si rifà); quando l'altezza di qualcosa sopra cambia (una resa
// che arriva, un elemento che si apre) lo scorrimento si corregge, così l'elemento in cima allo schermo resta dov'è.
// La vista scorre solo con i tasti ↑ ↓, per portare in vista l'elemento scelto.
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
    property string lessonTitle: ""                    // il suo titolo, anche se per un momento non è fra le righe
    readonly property var group: lesson !== "" ? groups.find(g => g.row.lesson === lesson) || null : null
    readonly property var cards: group ? group.cards : []
    readonly property var rowOf: {
        const out = {}
        for (const c of cards)
            out[keyOf(c)] = c
        return out
    }
    property string focusKey: ""                       // l'elemento evidenziato: i tasti valgono per lui
    readonly property var card: focusKey !== "" ? rowOf[focusKey] || null : null
    function isFocused(row) { return focusKey !== "" && keyOf(row) === focusKey }
    function pendingOf(g) { return g.cards.filter(c => isOpen(c)).length }
    onGroupChanged: if (group) lessonTitle = group.row.title || ""

    // gli elementi della lezione, per chiave: si tolgono quelli che non ci sono più, si aggiungono i nuovi al loro posto
    ListModel { id: entryModel }
    onCardsChanged: syncEntries()
    function syncEntries() {
        const keys = cards.map(c => keyOf(c))
        const wanted = {}
        for (const k of keys)
            wanted[k] = true
        for (let i = entryModel.count - 1; i >= 0; --i)
            if (!wanted[entryModel.get(i).key])
                entryModel.remove(i)
        for (let i = 0; i < keys.length; ++i) {
            if (i < entryModel.count && entryModel.get(i).key === keys[i])
                continue
            let at = -1
            for (let j = i + 1; j < entryModel.count; ++j)
                if (entryModel.get(j).key === keys[i]) {
                    at = j
                    break
                }
            if (at >= 0)
                entryModel.move(at, i, 1)
            else
                entryModel.insert(i, { key: keys[i] })
        }
    }

    function open(name) {
        const g = groups.find(x => x.row.lesson === name)
        if (!g)
            return back()
        entryModel.clear()                             // un'altra lezione: si parte dall'inizio
        lesson = name
        lessonTitle = g.row.title || ""
        const first = g.cards.find(c => isOpen(c)) || g.cards[0]
        focusKey = first ? keyOf(first) : ""
        list.contentY = 0
        anchor = { key: "", offset: 0 }
    }
    function back() { lesson = ""; focusKey = ""; entryModel.clear() }
    function stepLesson(by) {
        const at = groups.findIndex(g => g.row.lesson === lesson)
        if (at >= 0 && at + by >= 0 && at + by < groups.length)
            open(groups[at + by].row.lesson)
    }
    // con i tasti: un elemento prima o dopo, e lo si porta in vista
    function step(by) {
        if (cards.length === 0)
            return
        const at = cards.findIndex(c => keyOf(c) === focusKey)
        const to = at < 0 ? 0 : Math.max(0, Math.min(cards.length - 1, at + by))
        focusKey = keyOf(cards[to])
        reveal(focusKey)
    }
    // dopo una decisione su `row`: si evidenzia il prossimo elemento ancora da rivedere, senza scorrere (`show`: con i
    // tasti, lo si porta in vista)
    function next(row, show) {
        hold(row || card)
        const from = cards.findIndex(c => keyOf(c) === keyOf(row || card || {}))
        for (let i = from + 1; i < cards.length; ++i)
            if (isOpen(cards[i])) {
                focusKey = keyOf(cards[i])
                if (show)
                    reveal(focusKey)
                return
            }
    }
    function entryOf(key) {
        for (let i = 0; i < entries.count; ++i) {
            const e = entries.itemAt(i)
            if (e && e.key === key)
                return e
        }
        return null
    }
    function reveal(key) {
        const entry = entryOf(key)
        if (!entry)
            return
        const top = entryColumn.y + entry.y, bottom = top + entry.height
        if (top < list.contentY)
            scrollTo(top - Theme.gap)
        else if (bottom > list.contentY + list.height)
            scrollTo(Math.min(top - Theme.gap, bottom - list.height + Theme.gap))
    }

    // lo scorrimento ancorato: l'elemento in cima allo schermo e quanto ne è già passato
    property var anchor: ({ key: "", offset: 0 })
    property bool restoring: false
    function scrollTo(y) {
        // l'altezza del contenuto dalla colonna: `contentHeight` si aggiorna un attimo dopo
        const total = entryColumn.implicitHeight + 3 * Theme.gap
        list.contentY = Math.max(0, Math.min(y, Math.max(0, total - list.height)))
    }
    // l'ancora: il primo elemento che comincia dentro lo schermo (se nessuno, quello che lo riempie)
    function takeAnchor() {
        if (restoring)
            return
        let covering = null
        for (let i = 0; i < entries.count; ++i) {
            const e = entries.itemAt(i)
            if (!e)
                continue
            const top = entryColumn.y + e.y
            if (top >= list.contentY - 1)
                return anchorOn(e)
            if (top + e.height > list.contentY && !covering)
                covering = e
        }
        if (covering)
            return anchorOn(covering)
        anchor = { key: "", offset: 0 }
    }
    function anchorOn(e) { anchor = { key: e.key, offset: list.contentY - (entryColumn.y + e.y) } }
    // prima di cambiare `row` (una decisione): resta ferma lei, se si vede; ciò che cambia in lei cresce in giù
    function hold(row) {
        const e = row ? entryOf(keyOf(row)) : null
        if (!e)
            return
        const top = entryColumn.y + e.y
        if (top < list.contentY + list.height && top + e.height > list.contentY)
            anchorOn(e)
    }
    function keepAnchor() {
        if (anchor.key === "" || list.contentY <= 0 && anchor.offset <= 0)
            return
        const e = entryOf(anchor.key)
        if (!e)
            return
        restoring = true
        scrollTo(entryColumn.y + e.y + anchor.offset)
        restoring = false
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
            readonly property bool inside: deck.lesson !== ""
            readonly property int at: deck.groups.findIndex(g => g.row.lesson === deck.lesson)
            TxButton { visible: parent.inside; text: "←  Lezioni"; hint: "Tutte le lezioni (Esc)"; onClicked: deck.back() }
            TxLabel {
                Layout.fillWidth: true
                text: parent.inside ? deck.lessonTitle : deck.title
                font.pixelSize: Theme.textSize + 3
                font.weight: Font.DemiBold
            }
            TxLabel {
                visible: parent.inside
                text: deck.group && deck.pendingOf(deck.group) > 0 ? deck.pendingOf(deck.group) + " da rivedere su " + deck.cards.length
                      : deck.cards.length > 0 ? "tutto deciso" : ""
                color: deck.group && deck.pendingOf(deck.group) > 0 ? Theme.warn : Theme.textFaint
                font.pixelSize: Theme.smallSize
            }
            Slot { visible: deck.group !== null; sourceComponent: deck.group ? deck.lessonActions : null; row: deck.group ? deck.group.row : ({}); group: deck.group || ({ row: {}, cards: [] }) }
            TxButton { visible: parent.inside; text: "‹"; hint: "La lezione prima (←)"; enabled: parent.at > 0; onClicked: deck.stepLesson(-1) }
            TxButton { visible: parent.inside; text: "›"; hint: "La lezione dopo (→)"; enabled: parent.at >= 0 && parent.at < deck.groups.length - 1; onClicked: deck.stepLesson(1) }
            RowLayout { id: actionRow; visible: !parent.inside; spacing: Theme.gap }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // l'elenco delle lezioni
        TxScroll {
            id: lessonList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: deck.lesson === "" && deck.groups.length > 0
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
            visible: deck.lesson === "" && deck.groups.length === 0 || deck.lesson !== "" && deck.cards.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: deck.lesson === "" ? deck.emptyText : "In questa lezione non c'è niente da rivedere."
            color: Theme.textFaint
        }

        // una lezione: tutti i suoi elementi, interi
        TxScroll {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: deck.lesson !== "" && deck.cards.length > 0
            contentHeight: entryColumn.implicitHeight + 3 * Theme.gap
            onContentYChanged: deck.takeAnchor()
            onHeightChanged: deck.keepAnchor()
            Column {
                id: entryColumn
                x: 2 * Theme.gap + 4
                y: Theme.gap
                width: list.width - 4 * Theme.gap - 8
                spacing: Theme.gap
                onImplicitHeightChanged: deck.keepAnchor()
                Repeater {
                    id: entries
                    model: entryModel
                    Rectangle {
                        id: entry
                        required property string key
                        readonly property var row: deck.rowOf[key] || ({})
                        readonly property bool open: deck.isOpen(row)
                        readonly property bool focused: deck.focusKey === key
                        readonly property var badge: deck.badgeOf(row)
                        readonly property bool wide: width > 860
                        width: entryColumn.width
                        height: body.implicitHeight + 2 * Theme.gap + 4
                        radius: Theme.radius
                        color: Theme.surface
                        border.width: focused ? 2 : 1
                        border.color: focused ? Theme.accent : open ? Qt.rgba(badge.color.r, badge.color.g, badge.color.b, 0.7) : Theme.border
                        // un clic dove non c'è un tasto evidenzia l'elemento
                        MouseArea { anchors.fill: parent; onPressed: mouse => { deck.focusKey = entry.key; mouse.accepted = false } }
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
