import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Rivedi figure: tutte le figure delle lezioni, lezione per lezione. Di ognuna: com'è nella lezione e, se viene da
// un'altra (una pagina di una fonte, il codice di un altro autore), l'originale accanto; la didascalia per dire che
// cosa deve mostrare. Si accetta, si rigenera (con un commento facoltativo per il disegnatore) e, se viene da una fonte,
// si passa fra l'ultimo disegno e il ritaglio dalla pagina, che si sistema a mano: riquadro e cancellino.
Rectangle {
    id: root
    property var items: []
    signal lineAsked(string file, int line)
    signal figureAsked(string file, int line)          // «Vai alla figura»: il documento in quel punto, nell'anteprima
    signal adoptAsked()                                // ci sono figure senza segno: ProjectView salva le schede e le registra
    color: Theme.window

    property int revision: 0                           // le immagini si rileggono dopo ogni lavoro
    function reload() {
        items = Workspace.figureItems()
        if (Workspace.busy)
            return
        if (Workspace.unadoptedFigures() > 0)
            Qt.callLater(root.adoptAsked)
        else if (items.some(i => i.type === "figure" && (i.render === "" || (i.origin !== "" && i.original === ""))))
            Qt.callLater(Workspace.renderFigures)      // la resa e l'originale che mancano: si fanno in un lavoro
    }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    // le immagini si rileggono quando i file cambiano o un lavoro finisce; non a ogni decisione (non lampeggiano)
    Connections { target: Workspace; function onFilesChanged() { root.revision++; if (root.visible) root.reload() } }
    Connections { target: Workspace; function onTreeChanged() { if (root.visible) root.reload() } }
    Connections { target: Workspace; function onBusyChanged() { if (!Workspace.busy) { root.revision++; if (root.visible) root.reload() } } }

    // Dall'albero: la panoramica (la lezione è lì).
    function showLesson(lesson) { deck.current = -1 }
    function showShots() { if (deck.cards.length > 0) deck.current = 0 }   // per le prove: la prima scheda

    function file(path) { return path ? "file://" + encodeURI(path) + "?" + root.revision : "" }
    function stateText(row) {
        return ({ disegnata: "disegnata", ritagliata: "ritagliata dalla fonte", scritta: "scritta nella lezione",
                  da_inserire: "da inserire" })[row.state] || row.state
    }
    function accepted(row) { return row.decision === "accettata" }

    ReviewDeck {
        id: deck
        anchors.fill: parent
        visible: root.cropping === null
        title: "Rivedi figure"
        items: root.items
        emptyText: "Il progetto non ha figure."
        goLabel: "Vai alla figura"
        isOpen: row => !root.accepted(row)
        badgeOf: row => root.accepted(row) ? { text: "accettata", color: Theme.good } : { text: "da rivedere", color: Theme.warn }
        nameOf: row => row.name + " · " + root.stateText(row)
        hintOf: row => row.description || ""
        onGoAsked: row => root.figureAsked(row.lesson, row.line)
        tilePreview: preview
        stage: figureStage
        choices: figureChoices
        lessonActions: lessonButtons
        TxButton {
            text: "Accetta tutte"
            enabled: root.items.some(i => i.type === "figure" && !root.accepted(i))
            onClicked: Workspace.acceptAllFigures("", true)
        }
        TxButton {
            text: "Riapri tutte"
            enabled: root.items.some(i => i.type === "figure" && root.accepted(i))
            onClicked: Workspace.acceptAllFigures("", false)
        }
        TxButton {
            text: "Ricontrolla con le fonti"
            hint: "L'agente rilegge le fonti e propone un piano"
            enabled: !Workspace.busy
            onClicked: Workspace.recheckFigures()
        }
    }

    // ── i tasti: A accetta, R riapre, G rigenera, C modifica il ritaglio
    Shortcut {
        sequence: "A"
        enabled: root.visible && root.cropping === null && deck.card !== null
        onActivated: { Workspace.acceptFigure(deck.card.id, true); deck.next() }
    }
    Shortcut {
        sequence: "R"
        enabled: root.visible && root.cropping === null && deck.card !== null && root.accepted(deck.card)
        onActivated: Workspace.acceptFigure(deck.card.id, false)
    }
    Shortcut {
        sequence: "C"
        enabled: root.visible && root.cropping === null && deck.card !== null && (deck.card.pageImage || "") !== "" && !Workspace.busy
        onActivated: root.startCrop(deck.card)
    }

    // un'immagine su fondo bianco, con il suo titolo; un testo se non c'è ancora
    component Picture: ColumnLayout {
        id: picture
        property string title
        property string path
        property string missing
        property int maxHeight: 300
        spacing: 4
        TxLabel { visible: picture.title !== ""; Layout.fillWidth: true; text: picture.title; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: image.status === Image.Ready ? Math.min(width * image.sourceSize.height / Math.max(1, image.sourceSize.width) + 16, picture.maxHeight) : 60
            color: picture.path ? "white" : Theme.window
            radius: Theme.radius - 2
            border.width: picture.path ? 0 : 1
            border.color: Theme.border
            Image {
                id: image
                anchors.fill: parent
                anchors.margins: 8
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: true
                mipmap: true
                source: root.file(picture.path)
            }
            TxLabel {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.gap
                horizontalAlignment: Text.AlignHCenter
                visible: !picture.path
                text: picture.missing
                color: Theme.textFaint
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: preview
        Picture {
            property var row: ({})
            path: row.render || ""
            missing: Workspace.jobRunning ? "La preparo…" : "Non si compila da sola."
            maxHeight: 168
        }
    }

    Component {
        id: lessonButtons
        TxButton {
            property var row: ({})
            property var group: ({ cards: [] })
            visible: group.cards.some(c => !root.accepted(c.row))
            text: "Accetta tutte"
            hint: "Accetta tutte le figure di questa lezione"
            onClicked: Workspace.acceptAllFigures(row.lesson, true)
        }
    }

    // la figura com'è nella lezione e, se viene da un'altra, l'originale; sopra la didascalia
    Component {
        id: figureStage
        ColumnLayout {
            property var row: ({})
            spacing: Theme.gap
            TxRendered { visible: (row.description || "") !== ""; text: row.description || ""; textColor: Theme.textMuted; zoom: 0.62 }
            TxLabel { visible: !row.description; text: "Senza didascalia."; color: Theme.textFaint }
            GridLayout {                               // affiancate; una sotto l'altra se lo spazio è poco
                Layout.fillWidth: true
                columns: width > 700 ? 2 : 1
                columnSpacing: 2 * Theme.gap
                rowSpacing: Theme.gap
                Picture {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    title: "Nella lezione"
                    path: row.render || ""
                    maxHeight: 640
                    missing: Workspace.jobRunning ? "La preparo…" : "Non si compila da sola."
                }
                Picture {
                    visible: row.origin !== ""
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    title: row.origin === "fonte" ? "L'originale: " + row.source + ", pagina " + row.page : "L'originale: il codice dell'autore"
                    path: row.original || ""
                    maxHeight: 640
                    missing: Workspace.jobRunning ? "Lo preparo…" : "La fonte non si trova più."
                }
            }
            TxLabel {
                Layout.fillWidth: true
                visible: (row.note || "") !== ""
                text: row.note || ""
                color: Theme.bad
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
    }

    Component {
        id: figureChoices
        ColumnLayout {
            property var row: ({})
            spacing: Theme.gap
            TxChoice {
                text: "Accetta  (A)"
                tone: Theme.good
                chosen: root.accepted(row)
                onClicked: { Workspace.acceptFigure(row.id, true); deck.next() }
            }
            TxChoice { visible: root.accepted(row); text: "Riapri  (R)"; tone: Theme.idle; onClicked: Workspace.acceptFigure(row.id, false) }
            // disegno o ritaglio, per una figura che viene da una pagina
            TxSegmented {
                visible: row.origin === "fonte"
                Layout.fillWidth: true
                choices: [{ value: "disegnata", label: "Disegno" }, { value: "ritagliata", label: "Ritaglio dalla fonte" }]
                value: row.state
                enabled: !Workspace.busy
                onChosen: value => {
                    if (value === row.state)
                        return
                    if (value === "disegnata")
                        row.hasDrawing ? Workspace.useDrawing(row.id) : Workspace.redoFigure(row.id, "ridisegna")
                    else
                        Workspace.redoFigure(row.id, "ritaglia")
                }
            }
            TxChoice {
                visible: row.origin === "fonte"
                enabled: !Workspace.busy && (row.pageImage || "") !== ""
                text: "Modifica il ritaglio  (C)"
                tone: Theme.warn
                onClicked: root.startCrop(row)
            }
            Item { implicitHeight: Theme.gap / 2 }
            TxTextArea {
                id: comment
                Layout.fillWidth: true
                implicitHeight: 3 * Theme.rowHeight
                placeholder: "Che cosa cambiare rigenerandola (facoltativo)"
            }
            TxChoice {
                enabled: !Workspace.busy
                text: "Rigenera"
                tone: Theme.accent
                onClicked: { Workspace.redoFigure(row.id, "ridisegna", comment.text); comment.text = "" }
            }
            TxLabel {
                Layout.fillWidth: true
                text: row.origin === "" ? "Il disegnatore la rifà guardando com'è ora e la didascalia, con il commento se c'è."
                                        : "Il disegnatore la rifà dall'originale, con il commento se c'è."
                color: Theme.textFaint
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
    }

    // ── Il ritaglio a mano: il riquadro (bordi e angoli lo cambiano, l'interno lo sposta) e il cancellino (si trascina
    // sulla pagina per coprire di bianco ciò che non è della figura; ogni zona si sposta, si allarga e si toglie).
    property var cropping: null                        // la figura (riga di figureItems)
    property var cropBox: [0.1, 0.1, 0.9, 0.5]
    property var cropErase: []
    property string cropMode: "riquadro"
    function startCrop(row) {
        cropBox = row.box && row.box.length === 4 ? row.box.slice() : [0.1, 0.1, 0.9, 0.5]
        cropErase = (row.erase || []).map(z => z.slice())
        cropMode = "riquadro"
        cropping = row
    }
    function cropById(id) {
        const row = items.find(i => i.type === "figure" && i.id === id)
        if (row && row.pageImage)
            startCrop(row)
    }
    function clamp(v) { return Math.max(0, Math.min(1, v)) }
    function ordered(r) { return [Math.min(r[0], r[2]), Math.min(r[1], r[3]), Math.max(r[0], r[2]), Math.max(r[1], r[3])] }

    // un rettangolo che si sposta e si ridimensiona; `rect` in frazioni del foglio
    component Zone: Item {
        id: zone
        property var rect: [0, 0, 1, 1]
        property var live: null                        // durante il trascinamento
        readonly property var shown: live || rect
        property color tone: Theme.accent
        property bool erasing: false
        property bool removable: false
        property Item paper
        signal moved(var rect)
        signal removed()
        x: shown[0] * paper.width
        y: shown[1] * paper.height
        width: (shown[2] - shown[0]) * paper.width
        height: (shown[3] - shown[1]) * paper.height
        Rectangle {
            anchors.fill: parent
            color: zone.erasing ? "white" : "transparent"
            opacity: zone.erasing ? 0.9 : 1
            border.width: 2
            border.color: zone.tone
        }
        property var start: null
        function begin(mouse, item) {
            const p = item.mapToItem(paper, mouse.x, mouse.y)
            start = { x: p.x, y: p.y, rect: shown.slice() }
        }
        function drag(mouse, item, mx, my) {
            if (!start)
                return
            const p = item.mapToItem(paper, mouse.x, mouse.y)
            const dx = (p.x - start.x) / paper.width, dy = (p.y - start.y) / paper.height
            const r = start.rect.slice()
            if (mx === 0 && my === 0) {                // sposta, dentro il foglio
                const w = r[2] - r[0], h = r[3] - r[1]
                r[0] = Math.max(0, Math.min(1 - w, r[0] + dx)); r[2] = r[0] + w
                r[1] = Math.max(0, Math.min(1 - h, r[1] + dy)); r[3] = r[1] + h
            } else {
                if (mx < 0) r[0] = root.clamp(r[0] + dx)
                if (mx > 0) r[2] = root.clamp(r[2] + dx)
                if (my < 0) r[1] = root.clamp(r[1] + dy)
                if (my > 0) r[3] = root.clamp(r[3] + dy)
            }
            live = root.ordered(r)
        }
        function end() {
            if (live)
                moved(live)
            live = null
            start = null
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: 6
            cursorShape: Qt.SizeAllCursor
            onPressed: mouse => zone.begin(mouse, this)
            onPositionChanged: mouse => zone.drag(mouse, this, 0, 0)
            onReleased: zone.end()
        }
        Repeater {                                     // bordi e angoli
            model: [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]]
            Rectangle {
                required property var modelData
                readonly property int mx: modelData[0]
                readonly property int my: modelData[1]
                width: 12; height: 12; radius: 2
                color: zone.tone
                x: (mx + 1) / 2 * zone.width - width / 2
                y: (my + 1) / 2 * zone.height - height / 2
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: parent.mx === 0 ? Qt.SizeVerCursor : parent.my === 0 ? Qt.SizeHorCursor
                                 : parent.mx === parent.my ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                    onPressed: mouse => zone.begin(mouse, this)
                    onPositionChanged: mouse => zone.drag(mouse, this, parent.mx, parent.my)
                    onReleased: zone.end()
                }
            }
        }
        Rectangle {                                    // togli la zona
            visible: zone.removable
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: -10
            width: 20; height: 20; radius: 10
            color: removeArea.containsMouse ? Theme.bad : Theme.border
            Text { anchors.centerIn: parent; text: "×"; color: Theme.text; font.pixelSize: 14 }
            MouseArea { id: removeArea; anchors.fill: parent; hoverEnabled: true; onClicked: zone.removed() }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.cropping !== null
        color: Theme.window
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.gap + 4
            spacing: Theme.gap
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TxButton { text: "←  Annulla"; hint: "Torna alle figure senza cambiare il ritaglio"; onClicked: root.cropping = null }
                TxLabel {
                    Layout.fillWidth: true
                    text: root.cropping ? root.cropping.name + " · ritaglio" : ""
                    font.pixelSize: Theme.textSize + 2
                    font.weight: Font.DemiBold
                }
                TxSegmented {
                    choices: [{ value: "riquadro", label: "Riquadro" }, { value: "cancellino", label: "Cancellino" }]
                    value: root.cropMode
                    onChosen: value => root.cropMode = value
                }
                TxButton {
                    text: "Togli il cancellino"
                    enabled: root.cropErase.length > 0
                    onClicked: root.cropErase = []
                }
                TxButton {
                    text: "Applica"
                    primary: true
                    enabled: !Workspace.busy
                    onClicked: {
                        Workspace.cropFigure(root.cropping.id, root.cropBox, root.cropErase)
                        root.cropping = null
                    }
                }
            }
            TxLabel {
                Layout.fillWidth: true
                text: (root.cropping ? root.cropping.source + ", pagina " + root.cropping.page + ". " : "") + (root.cropMode === "riquadro"
                      ? "Trascina i bordi e gli angoli del riquadro per dire dove sta la figura; l'interno lo sposta."
                      : "Trascina sulla pagina per coprire di bianco ciò che non è della figura (testo accanto, numeri). Ogni zona si sposta, si allarga e si toglie con la ×.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Item {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                readonly property real aspect: pageImage.implicitHeight > 0 ? pageImage.implicitWidth / pageImage.implicitHeight : 0.707
                Item {
                    id: sheet
                    width: Math.min(stage.width, stage.height * stage.aspect)
                    height: width / stage.aspect
                    anchors.centerIn: parent
                    Image {
                        id: pageImage
                        anchors.fill: parent
                        source: root.cropping ? "file://" + encodeURI(root.cropping.pageImage) : ""
                        fillMode: Image.Stretch                // il foglio ha già le sue proporzioni: riquadro e zone ci stanno sopra esatti
                        asynchronous: true
                        mipmap: true
                    }
                    // fuori dal riquadro è più scuro
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            readonly property var r: boxZone.shown
                            color: "black"
                            opacity: 0.35
                            x: index === 1 ? r[2] * sheet.width : 0
                            y: index === 0 ? 0 : index === 2 ? r[3] * sheet.height : r[1] * sheet.height
                            width: index === 0 || index === 2 ? sheet.width : index === 1 ? (1 - r[2]) * sheet.width : r[0] * sheet.width
                            height: index === 0 ? r[1] * sheet.height : index === 2 ? (1 - r[3]) * sheet.height : (r[3] - r[1]) * sheet.height
                        }
                    }
                    // il cancellino: trascinare su un posto vuoto fa una zona nuova
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.cropMode === "cancellino"
                        cursorShape: Qt.CrossCursor
                        property var from: null
                        onPressed: mouse => {
                            from = [root.clamp(mouse.x / sheet.width), root.clamp(mouse.y / sheet.height)]
                            drawing.rect = [from[0], from[1], from[0], from[1]]
                            drawing.visible = true
                        }
                        onPositionChanged: mouse => {
                            if (from)
                                drawing.rect = root.ordered([from[0], from[1], root.clamp(mouse.x / sheet.width), root.clamp(mouse.y / sheet.height)])
                        }
                        onReleased: {
                            const r = drawing.rect
                            drawing.visible = false
                            from = null
                            if (r[2] - r[0] > 0.004 && r[3] - r[1] > 0.004)
                                root.cropErase = root.cropErase.concat([r])
                        }
                    }
                    Rectangle {
                        id: drawing
                        property var rect: [0, 0, 0, 0]
                        visible: false
                        x: rect[0] * sheet.width; y: rect[1] * sheet.height
                        width: (rect[2] - rect[0]) * sheet.width; height: (rect[3] - rect[1]) * sheet.height
                        color: "white"
                        opacity: 0.9
                        border.width: 2
                        border.color: Theme.bad
                    }
                    Repeater {
                        model: root.cropErase
                        Zone {
                            required property var modelData
                            required property int index
                            paper: sheet
                            rect: modelData
                            tone: Theme.bad
                            erasing: true
                            removable: root.cropMode === "cancellino"
                            enabled: root.cropMode === "cancellino"
                            onMoved: r => { const all = root.cropErase.slice(); all[index] = r; root.cropErase = all }
                            onRemoved: { const all = root.cropErase.slice(); all.splice(index, 1); root.cropErase = all }
                        }
                    }
                    Zone {
                        id: boxZone
                        paper: sheet
                        rect: root.cropBox
                        tone: Theme.accent
                        enabled: root.cropMode === "riquadro"
                        opacity: enabled ? 1 : 0.6
                        onMoved: r => root.cropBox = r
                    }
                }
            }
        }
    }
    Shortcut { sequence: "Escape"; enabled: root.visible && root.cropping !== null; onActivated: root.cropping = null }
}
