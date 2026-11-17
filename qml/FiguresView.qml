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
        revision++
        if (Workspace.busy)
            return
        if (Workspace.unadoptedFigures() > 0)
            Qt.callLater(root.adoptAsked)
        else if (items.some(i => i.type === "figure" && (i.render === "" || (i.origin !== "" && i.original === ""))))
            Qt.callLater(Workspace.renderFigures)      // la resa e l'originale che mancano: si fanno in un lavoro
    }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onFilesChanged() { if (root.visible) root.reload() } }
    Connections { target: Workspace; function onTreeChanged() { if (root.visible) root.reload() } }
    Connections { target: Workspace; function onBusyChanged() { if (root.visible && !Workspace.busy) root.reload() } }

    // Porta la vista su una lezione (dall'albero).
    function showLesson(lesson) {
        for (let i = 0; i < items.length; ++i)
            if (items[i].type === "lesson" && items[i].lesson === lesson)
                return list.positionViewAtIndex(i, ListView.Beginning)
    }
    // la didascalia come nel PDF (intanto il testo; se non compila resta il testo)
    property int renderTick: 0
    Connections { target: Workspace; function onSnippetsReady() { root.renderTick++ } }
    readonly property string ink: ("" + Theme.textMuted).slice(-6)
    component Rendered: Item {
        id: piece
        property string text
        readonly property string path: text !== "" && root.renderTick >= 0 ? Workspace.snippet(text, "prosa", root.ink) : ""
        readonly property bool ready: picture.status === Image.Ready
        implicitHeight: ready ? picture.height : label.implicitHeight
        TxLabel {
            id: label
            visible: !piece.ready
            width: parent.width
            text: piece.text
            color: Theme.textMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Image {
            id: picture
            visible: piece.ready
            source: piece.path !== "" ? "file://" + piece.path : ""
            asynchronous: true
            width: Math.min(piece.width, sourceSize.width * 0.52)
            height: sourceSize.width > 0 ? width * sourceSize.height / sourceSize.width : 0
            smooth: true
            mipmap: true
        }
    }
    function file(path) { return path ? "file://" + encodeURI(path) + "?" + root.revision : "" }
    function stateText(row) {
        return ({ disegnata: "disegnata", ritagliata: "ritagliata dalla fonte", scritta: "scritta nella lezione",
                  da_inserire: "da inserire" })[row.state] || row.state
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.cropping === null
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gap + 4
            TxLabel { Layout.fillWidth: true; text: "Rivedi figure"; font.pixelSize: Theme.textSize + 3; font.weight: Font.DemiBold }
            TxButton {
                text: "Ricontrolla con le fonti"
                hint: "L'agente rilegge le fonti e propone un piano"
                enabled: !Workspace.busy
                onClicked: Workspace.recheckFigures()
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 2 * Theme.gap + 4      // staccata dal bordo
            Layout.rightMargin: Theme.gap
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.gap
            bottomMargin: 2 * Theme.gap
            model: root.items
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                required property var modelData
                width: Math.min(list.width - 14, 1100)
                sourceComponent: modelData.type === "lesson" ? head : card
                onLoaded: item.row = modelData
                onModelDataChanged: if (item) item.row = modelData
            }
        }
    }
    TxLabel { anchors.centerIn: parent; visible: root.items.length === 0 && root.cropping === null; text: "Il progetto non ha figure."; color: Theme.textFaint }

    Component {
        id: head
        TxLabel { property var row: ({}); topPadding: Theme.gap; text: row.title || ""; font.weight: Font.DemiBold }
    }

    // un'immagine su fondo bianco, con il suo titolo; un testo se non c'è ancora
    component Picture: ColumnLayout {
        property string title
        property string path
        property string missing
        spacing: 4
        TxLabel { Layout.fillWidth: true; text: parent.title; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: picture.status === Image.Ready ? Math.min(width * picture.sourceSize.height / Math.max(1, picture.sourceSize.width) + 16, 300) : 60
            color: parent.path ? "white" : Theme.window
            radius: Theme.radius - 2
            border.width: parent.path ? 0 : 1
            border.color: Theme.border
            Image {
                id: picture
                anchors.fill: parent
                anchors.margins: 8
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: true
                mipmap: true
                source: root.file(parent.parent.path)
            }
            TxLabel {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.gap
                horizontalAlignment: Text.AlignHCenter
                visible: !parent.parent.path
                text: parent.parent.missing
                color: Theme.textFaint
                font.pixelSize: Theme.smallSize
            }
        }
    }

    Component {
        id: card
        Rectangle {
            id: box
            property var row: ({})
            readonly property bool accepted: row.decision === "accettata"
            readonly property bool fromSource: row.origin === "fonte"
            implicitHeight: column.implicitHeight + 2 * Theme.gap + 4
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: accepted ? Theme.border : Theme.warn
            ColumnLayout {
                id: column
                anchors.fill: parent
                anchors.margins: Theme.gap + 2
                spacing: Theme.gap
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxLabel { text: box.row.name || ""; font.weight: Font.Medium }
                    Rectangle {
                        implicitWidth: badge.implicitWidth + 12
                        implicitHeight: badge.implicitHeight + 4
                        radius: 3
                        readonly property color tone: box.accepted ? Theme.good : Theme.warn
                        color: Qt.rgba(tone.r, tone.g, tone.b, 0.25)
                        Text { id: badge; anchors.centerIn: parent; text: box.accepted ? "accettata" : "da rivedere"; color: Theme.text; font.pixelSize: Theme.smallSize - 1; font.weight: Font.DemiBold }
                    }
                    TxLabel { Layout.fillWidth: true; text: root.stateText(box.row); color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                    TxButton { text: "Vai alla figura"; flat: true; hint: "La mostra nell'anteprima del documento"; onClicked: root.figureAsked(box.row.lesson, box.row.line) }
                }
                Rendered {
                    Layout.fillWidth: true
                    visible: (box.row.description || "") !== ""
                    text: box.row.description || ""
                }
                TxLabel { visible: !box.row.description; text: "Senza didascalia."; color: Theme.textFaint }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 2 * Theme.gap
                    Picture {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        title: "Nella lezione"
                        path: box.row.render || ""
                        missing: Workspace.jobRunning ? "La preparo…" : "Non si compila da sola."
                    }
                    Picture {
                        visible: box.row.origin !== ""
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        title: box.fromSource ? "L'originale: " + box.row.source + ", pagina " + box.row.page : "L'originale: il codice dell'autore"
                        path: box.row.original || ""
                        missing: Workspace.jobRunning ? "Lo preparo…" : "La fonte non si trova più."
                    }
                }
                TxLabel {
                    Layout.fillWidth: true
                    visible: (box.row.note || "") !== ""
                    text: box.row.note || ""
                    color: Theme.bad
                    font.pixelSize: Theme.smallSize
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                // disegno o ritaglio, per una figura che viene da una pagina
                RowLayout {
                    Layout.fillWidth: true
                    visible: box.fromSource
                    spacing: Theme.gap
                    TxSegmented {
                        choices: [{ value: "disegnata", label: "Disegno" }, { value: "ritagliata", label: "Ritaglio dalla fonte" }]
                        value: box.row.state
                        enabled: !Workspace.busy
                        onChosen: value => {
                            if (value === box.row.state)
                                return
                            if (value === "disegnata") {
                                if (box.row.hasDrawing)
                                    Workspace.useDrawing(box.row.id)
                                else
                                    Workspace.redoFigure(box.row.id, "ridisegna")
                            } else {
                                Workspace.redoFigure(box.row.id, "ritaglia")
                            }
                        }
                    }
                    TxButton {
                        text: "Modifica il ritaglio"
                        hint: "Riquadro e cancellino sulla pagina della fonte"
                        enabled: !Workspace.busy && (box.row.pageImage || "") !== ""
                        onClicked: root.startCrop(box.row)
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxTextArea {
                        id: comment
                        Layout.fillWidth: true
                        implicitHeight: Theme.rowHeight + 2 * Theme.gap
                        placeholder: "Che cosa cambiare rigenerandola (facoltativo)"
                    }
                    TxButton {
                        Layout.alignment: Qt.AlignTop
                        text: "Rigenera"
                        hint: "Il disegnatore la rifà" + (box.row.origin === "" ? " guardando com'è ora" : " dall'originale") + ", con il commento se c'è"
                        enabled: !Workspace.busy
                        onClicked: { Workspace.redoFigure(box.row.id, "ridisegna", comment.text); comment.text = "" }
                    }
                    TxButton {
                        Layout.alignment: Qt.AlignTop
                        text: box.accepted ? "Riapri" : "Accetta"
                        primary: !box.accepted
                        onClicked: Workspace.acceptFigure(box.row.id, !box.accepted)
                    }
                }
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
