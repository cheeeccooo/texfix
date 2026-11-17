import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// La forma comune delle revisioni, quella di Rivedi impaginazione. La panoramica: lezione per lezione, una scheda
// piccola per ogni cosa da rivedere (ciò che è deciso è oscurato); un clic apre la scheda, con a sinistra ciò che c'è da
// guardare e a destra le scelte. Frecce per la scheda prima e dopo, Esc per la panoramica. Chi la usa dà le righe
// (`items`: type «lesson» e le altre) e i pezzi: l'anteprima nella scheda piccola, la scena e le scelte della scheda, i
// tasti di una lezione e quelli in alto.
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
    property var keyOf: row => JSON.stringify([row.type, row.lesson, row.id || row.hash || "", row.line || 0])
    property string goLabel: "Apri alla riga"
    property Component tilePreview                     // l'anteprima della scheda piccola (row)
    property Component stage                           // a sinistra nella scheda (row)
    property Component choices                         // a destra nella scheda (row)
    property Component lessonActions                   // i tasti di una lezione (row: la lezione; group)
    default property alias actions: actionRow.data     // i tasti in alto, nella panoramica
    signal goAsked(var row)

    property int current: -1                           // la scheda aperta; -1: la panoramica
    readonly property var cards: items.filter(i => i.type !== "lesson")
    readonly property var card: current >= 0 && current < cards.length ? cards[current] : null
    // le lezioni con le loro schede: row (la riga della lezione), cards (con index fra le schede)
    readonly property var groups: {
        const out = []
        let n = 0
        for (const item of items) {
            if (item.type === "lesson")
                out.push({ row: item, cards: [] })
            else if (out.length > 0)
                out[out.length - 1].cards.push({ index: n++, row: item })
        }
        return out
    }
    readonly property var cardGroup: card ? groups.find(g => g.cards.some(c => c.index === current)) || null : null

    // la scheda aperta resta la stessa quando le righe si rileggono (una decisione le rilegge)
    property string currentKey: ""
    onCurrentChanged: currentKey = card ? keyOf(card) : ""
    onItemsChanged: {
        if (currentKey === "")
            return
        const at = cards.findIndex(c => keyOf(c) === currentKey)
        current = at >= 0 ? at : Math.min(current, cards.length - 1)
    }
    function step(by) { if (card) current = Math.max(0, Math.min(cards.length - 1, current + by)) }
    // dopo una decisione: alla prossima scheda ancora da rivedere, se c'è
    function next() {
        for (let i = current + 1; i < cards.length; ++i)
            if (isOpen(cards[i]))
                return current = i
    }

    Shortcut { sequence: "Escape"; enabled: deck.visible && deck.card !== null; onActivated: deck.current = -1 }
    Shortcut { sequence: "Left"; enabled: deck.visible && deck.card !== null; onActivated: deck.step(-1) }
    Shortcut { sequence: "Right"; enabled: deck.visible && deck.card !== null; onActivated: deck.step(1) }

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
        property bool large: false
        implicitWidth: badgeText.implicitWidth + (large ? 14 : 12)
        implicitHeight: badgeText.implicitHeight + (large ? 6 : 4)
        radius: 3
        visible: badge.text !== ""
        color: Qt.rgba(badge.color.r, badge.color.g, badge.color.b, large ? 0.28 : 0.25)
        Text { id: badgeText; anchors.centerIn: parent; text: parent.badge.text; color: Theme.text; font.pixelSize: parent.large ? Theme.smallSize : Theme.smallSize - 1; font.weight: Font.DemiBold }
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
            TxButton { visible: deck.card !== null; text: "←  Panoramica"; hint: "Tutte le schede (Esc)"; onClicked: deck.current = -1 }
            TxLabel {
                Layout.fillWidth: true
                text: deck.card ? "Scheda " + (deck.current + 1) + " di " + deck.cards.length : deck.title
                font.pixelSize: Theme.textSize + 3
                font.weight: Font.DemiBold
            }
            RowLayout { id: actionRow; visible: deck.card === null; spacing: Theme.gap }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        // la panoramica
        TxScroll {
            id: overview
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: deck.card === null && deck.items.length > 0
            contentHeight: grid.implicitHeight + 3 * Theme.gap
            Column {
                id: grid
                x: 2 * Theme.gap + 4
                y: Theme.gap
                width: overview.width - 4 * Theme.gap - 8
                spacing: 2 * Theme.gap
                Repeater {
                    model: deck.groups
                    Column {
                        id: lessonGroup
                        required property var modelData
                        readonly property int pending: modelData.cards.filter(c => deck.isOpen(c.row)).length
                        width: grid.width
                        spacing: Theme.gap
                        RowLayout {
                            width: parent.width
                            spacing: Theme.gap
                            TxLabel { text: lessonGroup.modelData.row.title || ""; font.weight: Font.DemiBold; font.pixelSize: Theme.textSize + 1 }
                            TxLabel {
                                Layout.fillWidth: true
                                text: lessonGroup.modelData.cards.length + (lessonGroup.pending > 0 ? " · " + lessonGroup.pending + " da rivedere" : " · tutto deciso")
                                      + (lessonGroup.modelData.row.done === true ? " · lezione fatta" : "")
                                color: lessonGroup.pending > 0 ? Theme.warn : Theme.textFaint
                                font.pixelSize: Theme.smallSize
                            }
                            Slot { sourceComponent: deck.lessonActions; row: lessonGroup.modelData.row; group: lessonGroup.modelData }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.border }
                        Flow {
                            width: parent.width
                            spacing: Theme.gap + 2
                            Repeater {
                                model: lessonGroup.modelData.cards
                                Rectangle {
                                    id: tile
                                    required property var modelData
                                    readonly property var row: modelData.row
                                    readonly property bool open: deck.isOpen(row)
                                    readonly property var badge: deck.badgeOf(row)
                                    width: 232
                                    height: 268
                                    radius: Theme.radius
                                    color: tileArea.containsMouse ? Theme.hover : Theme.surface
                                    border.width: open ? 2 : 1
                                    border.color: open ? badge.color : Theme.border
                                    opacity: open ? 1 : 0.5            // ciò che è deciso è oscurato
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: Theme.gap
                                        spacing: 6
                                        Badge { badge: tile.badge }
                                        TxLabel {
                                            width: parent.width
                                            height: 2 * (Theme.textSize + 5)
                                            text: deck.nameOf(tile.row)
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 2
                                            font.weight: Font.Medium
                                        }
                                        Slot {
                                            width: parent.width
                                            height: 168
                                            clip: true
                                            sourceComponent: deck.tilePreview
                                            row: tile.row
                                        }
                                    }
                                    MouseArea { id: tileArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: deck.current = tile.modelData.index }
                                    ToolTip.visible: tileArea.containsMouse && deck.hintOf(tile.row) !== ""
                                    ToolTip.delay: 700
                                    ToolTip.text: deck.hintOf(tile.row)
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
            visible: deck.card === null && deck.items.length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: deck.emptyText
            color: Theme.textFaint
        }

        // una scheda: a sinistra ciò che c'è da guardare, a destra le scelte
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.gap + 4
            Layout.leftMargin: 2 * Theme.gap + 4
            visible: deck.card !== null
            spacing: 2 * Theme.gap
            TxScroll {
                id: stageScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: stageSlot.implicitHeight + Theme.gap
                Slot {
                    id: stageSlot
                    width: stageScroll.width - 14
                    sourceComponent: deck.card ? deck.stage : null
                    row: deck.card
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 340
                Layout.maximumWidth: 340
                Layout.fillHeight: true
                spacing: Theme.gap
                Badge { badge: deck.card ? deck.badgeOf(deck.card) : ({ text: "", color: Theme.idle }); large: true }
                TxLabel { Layout.fillWidth: true; text: deck.card ? deck.nameOf(deck.card) : ""; font.weight: Font.DemiBold; font.pixelSize: Theme.textSize + 2; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                TxLabel { Layout.fillWidth: true; text: deck.cardGroup ? deck.cardGroup.row.title : ""; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                Item { implicitHeight: Theme.gap / 2 }
                Slot {
                    Layout.fillWidth: true
                    sourceComponent: deck.card ? deck.choices : null
                    row: deck.card
                }
                RowLayout {
                    Layout.fillWidth: true
                    TxButton { text: "←"; hint: "La scheda prima"; enabled: deck.current > 0; onClicked: deck.step(-1) }
                    Item { Layout.fillWidth: true }
                    TxButton { visible: deck.card !== null && deck.card.line > 0; text: deck.goLabel; flat: true; onClicked: deck.goAsked(deck.card) }
                    Item { Layout.fillWidth: true }
                    TxButton { text: "→"; hint: "La scheda dopo"; enabled: deck.current < deck.cards.length - 1; onClicked: deck.step(1) }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
