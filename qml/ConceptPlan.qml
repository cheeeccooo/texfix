import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Il piano di Aggiungi materiale per concetti (core/Sort, formato «concetti»): in cima, se il corso non c'è ancora, la
// scaletta in breve (con che cosa cambiare e il programma del corso), poi il grafo di dove va il materiale (dai file nuovi alla scaletta; gli archi fra file
// che si intersecano), poi i concetti in comune con la decisione, poi dove va ciò che resta di quei file, poi ciò che è tutto
// nuovo; in fondo le versioni, ciò che è già nel corso, le impostazioni e i tasti. Ogni modifica passa da Workspace.
Rectangle {
    id: root
    required property string planId
    property var plan: ({})
    property bool instructions: false
    property bool confirmDiscard: false
    property string focusKey: ""                       // un nome scelto nel grafo: si vedono solo i suoi concetti
    signal closeAsked()
    color: Theme.window

    function reload() {
        const y = scroll.contentY
        plan = Workspace.plan(planId)
        instructions = Workspace.planHasInstructions(planId)
        Qt.callLater(() => scroll.contentY = Math.max(0, Math.min(y, scroll.contentHeight - scroll.height)))
    }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections {
        target: Workspace
        // un programma del corso nuovo (o tolto) rifà la mappa: «Avvia» diventa «Rivedi piano»
        function onSettingsChanged() { root.instructions = Workspace.planHasInstructions(root.planId) }
        // dopo il gestore del clic: rileggere ricrea le righe, anche quella che ha ricevuto il clic
        function onPlanEdited(id) { if (id === root.planId) Qt.callLater(root.reload) }
        function onPlanReady(id) { if (id === root.planId) Qt.callLater(root.reload) }
    }
    readonly property bool editable: plan.stato === "piano"

    // chi è chi: le fonti nuove (un colore per file) e le lezioni del corso
    readonly property var holders: {
        const out = {}
        const files = []
        for (const s of plan.fonti || []) {
            if (!files.includes(s.file))
                files.push(s.file)
            out[s.key] = { name: s.name, course: false, color: Theme.sourceColor(files.indexOf(s.file)) }
        }
        for (const c of plan.corso || [])
            out[c.key] = { name: c.name, course: true, color: Theme.textMuted }
        return out
    }
    readonly property var fileColors: {
        const out = []
        const seen = []
        for (const s of plan.fonti || [])
            if (!seen.includes(s.file)) {
                seen.push(s.file)
                out.push({ file: s.file, color: Theme.sourceColor(seen.length - 1) })
            }
        return out
    }
    function holderOf(id) { return id.split(".")[0] }
    function nameOf(key) { return root.holders[key] ? root.holders[key].name : key.startsWith("new:") ? key.slice(4) : key }
    function colorOf(key) { return root.holders[key] ? root.holders[key].color : key.startsWith("new:") ? Theme.accent : Theme.textFaint }
    function titleOf(id) { const c = (root.plan.concetti || {})[id]; return c ? c.title : id }
    function whatOf(id) { const c = (root.plan.concetti || {})[id]; return c ? c.what : "" }
    readonly property var sharedIds: (plan.comuni || []).reduce((all, g) => all.concat(Array.from(g.items)), [])
    readonly property var sharedHolders: sharedIds.map(holderOf).filter((k, i, a) => a.indexOf(k) === i)
    function touches(d) { return d.concepts.some(id => root.sharedHolders.includes(root.holderOf(id))) }
    // Un titolo o un'etichetta, leggibile: la matematica breve come si scrive a mano («h_ie» con il pedice, «\tau» come τ),
    // per i testi su una riga; la prosa vera passa da TxRendered, compilata come nel PDF.
    function pretty(text) {
        const greek = { alpha: "α", beta: "β", gamma: "γ", delta: "δ", epsilon: "ε", varepsilon: "ε", theta: "θ", lambda: "λ", mu: "μ", pi: "π",
                        rho: "ρ", sigma: "σ", tau: "τ", phi: "φ", varphi: "φ", omega: "ω", Omega: "Ω", Delta: "Δ", infty: "∞", cdot: "·", times: "×",
                        to: "→", le: "≤", ge: "≥", approx: "≈", parallel: "∥", pm: "±", ne: "≠" }
        let s = (text || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        s = s.replace(/\\frac\{([^}]*)\}\{([^}]*)\}/g, "$1/$2")
             .replace(/\\([A-Za-z]+)/g, (m, name) => greek[name] !== undefined ? greek[name] : "")
             .replace(/\$/g, "")
             .replace(/([A-Za-zα-ωΑ-Ω])_\{([^}]*)\}/g, "<i>$1</i><sub>$2</sub>")
             .replace(/([A-Za-zα-ωΑ-Ω])_([A-Za-z0-9]+)/g, "<i>$1</i><sub>$2</sub>")
             .replace(/\^\{([^}]*)\}/g, "<sup>$1</sup>")
             .replace(/\^([A-Za-z0-9+-]+)/g, "<sup>$1</sup>")
        return s
    }
    // un suggerimento con la prosa compilata come nel PDF: largo quanto il testo, fino a un limite di lettura
    component LatexTip: ToolTip {
        id: tip
        delay: 450
        width: Math.min(rendered.implicitWidth + leftPadding + rightPadding, 480)
        contentItem: TxRendered { id: rendered; text: tip.text; fit: false; zoom: 0.45 }
    }
    // il nome di una parte di un riquadro: piccolo, in maiuscolo, sempre lo stesso posto
    component Caption: TxLabel {
        Layout.fillWidth: true
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize - 1
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
    }
    function dateWords(text) {
        const p = (text || "").split("/")
        if (p.length !== 3)
            return ""
        return new Date(+p[2], +p[1] - 1, +p[0]).toLocaleDateString(Qt.locale("it_IT"), "d MMMM")
    }

    // il grafo: quante volte due nomi compaiono nello stesso gruppo di concetti in comune
    readonly property var pairs: {
        const out = {}
        for (const g of plan.comuni || []) {
            const keys = g.items.map(holderOf).filter((k, i, a) => a.indexOf(k) === i)
            for (let i = 0; i < keys.length; ++i)
                for (let j = i + 1; j < keys.length; ++j) {
                    const k = [keys[i], keys[j]].sort().join("|")
                    out[k] = (out[k] || 0) + 1
                }
        }
        return out
    }
    readonly property var leftKeys: (plan.fonti || []).map(s => s.key)

    // la scaletta: il corso come sarà, le lezioni che hai e quelle nuove al loro posto (dopo quella che dice «after»; le
    // nuove con la data in ordine di data, quelle senza posto in fondo). Una lezione nuova ha la chiave «new:<titolo>».
    function unitKey(d) { return d.into === "new" ? "new:" + d.title : d.into }
    readonly property var units: {
        const dests = plan.destinazioni || []
        const seq = (plan.corso || []).map(c => c.key)
        const pending = dests.filter(d => d.into === "new")
                             .sort((a, b) => (a.date || "").split("/").reverse().join("").localeCompare((b.date || "").split("/").reverse().join("")))
        let firsts = 0
        while (pending.length) {
            let moved = false
            for (let j = 0; j < pending.length; ++j) {
                const after = (pending[j].after || "").trim()
                const at = after === "first" ? firsts++ : after === "" ? seq.length : seq.indexOf(after) >= 0 ? seq.indexOf(after) + 1 : -1
                if (at < 0)
                    continue
                seq.splice(at, 0, unitKey(pending[j]))
                pending.splice(j--, 1)
                moved = true
            }
            if (!moved) {                              // un «after» che non c'è: in fondo, nell'ordine del piano
                for (const d of pending)
                    seq.push(unitKey(d))
                break
            }
        }
        return seq.map(key => {
            const index = dests.findIndex(d => unitKey(d) === key)
            const d = index >= 0 ? dests[index] : null
            return { key: key, fresh: key.startsWith("new:"), title: key.startsWith("new:") ? key.slice(4) : nameOf(key),
                     index: index, date: d ? d.date || "" : "", fare: d ? d.fare : true, count: d ? d.concepts.length : 0,
                     from: d ? d.concepts.map(holderOf).filter((k, i, a) => a.indexOf(k) === i && leftKeys.includes(k)) : [] }
        })
    }
    // dove finisce un concetto: la lezione del corso che lo ha, o quella della scaletta che lo riceve
    function unitOf(id) {
        const h = holderOf(id)
        if (holders[h] && holders[h].course)
            return h
        const d = (plan.destinazioni || []).find(d => d.concepts.includes(id))
        return d ? unitKey(d) : ""
    }
    // a destra nel grafo: le lezioni nuove e quelle che hai che ricevono qualcosa, in ordine di scaletta
    readonly property var rightKeys: units.filter(u => u.fresh || u.index >= 0).map(u => u.key)
    // le linee: quanti concetti di un file nuovo vanno in una lezione della scaletta
    readonly property var flows: {
        const out = {}
        for (const d of plan.destinazioni || [])
            for (const id of d.concepts) {
                const h = holderOf(id)
                if (leftKeys.includes(h)) {
                    const k = h + "|" + unitKey(d)
                    out[k] = (out[k] || 0) + 1
                }
            }
        return out
    }

    component Chip: Rectangle {
        id: chip
        property string key
        property string label: root.nameOf(key)
        property string tip
        implicitWidth: Math.min(chipText.implicitWidth + 30, 320)
        implicitHeight: 22
        radius: 11
        color: Theme.panel
        border.width: 1
        border.color: Theme.border
        Rectangle { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: root.colorOf(chip.key) }
        TxLabel { id: chipText; x: 22; width: parent.width - 30; anchors.verticalCenter: parent.verticalCenter; text: chip.label; font.pixelSize: Theme.smallSize }
        MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        LatexTip { text: chip.tip; visible: chipArea.containsMouse && chip.tip !== "" }
    }
    component Heading: ColumnLayout {
        property string title
        property string detail
        Layout.fillWidth: true
        Layout.topMargin: Theme.gap
        spacing: 2
        TxLabel { Layout.fillWidth: true; text: parent.title; font.pixelSize: Theme.textSize + 2; font.weight: Font.DemiBold }
        TxLabel { Layout.fillWidth: true; visible: text !== ""; text: parent.detail; color: Theme.textMuted; font.pixelSize: Theme.smallSize; wrapMode: Text.WordWrap; elide: Text.ElideNone }
    }
    // un concetto, piccolo: il titolo leggibile, il colore della sua fonte; passandoci sopra, che cosa dice (compilato)
    component ConceptChip: Rectangle {
        id: concept
        property string conceptId
        property real maxWidth: 400
        readonly property bool shared: root.sharedIds.includes(conceptId)
        implicitWidth: Math.min(conceptText.implicitWidth + 22, maxWidth)
        implicitHeight: Math.max(26, conceptText.implicitHeight + 8)
        radius: Theme.radius
        color: Theme.panel
        border.width: 1
        border.color: conceptArea.containsMouse ? Theme.textFaint : Theme.border
        opacity: shared ? 0.55 : 1
        Rectangle { width: 3; height: parent.height - 8; x: 4; y: 4; radius: 1.5; color: root.colorOf(root.holderOf(concept.conceptId)) }
        Text {
            id: conceptText
            x: 14
            width: Math.min(implicitWidth, concept.maxWidth - 22)
            anchors.verticalCenter: parent.verticalCenter
            text: root.pretty(root.titleOf(concept.conceptId)) + (concept.shared ? " <font color=\"" + Theme.textMuted + "\">· in comune</font>" : "")
            textFormat: Text.RichText
            color: Theme.text
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }
        MouseArea { id: conceptArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        LatexTip { text: root.whatOf(concept.conceptId); visible: conceptArea.containsMouse && text !== "" }
    }
    // una destinazione: lezione nuova o aggiunta, da quali fonti, i suoi concetti, la spunta e le istruzioni
    component Destination: Rectangle {
        id: dest
        property var item
        property int index
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: destColumn.implicitHeight + 2 * Theme.gap + 4
        radius: Theme.radius + 2
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        opacity: item.fare ? 1 : 0.55
        visible: root.focusKey === "" || root.unitKey(item) === root.focusKey || item.concepts.some(id => root.holderOf(id) === root.focusKey)
        ColumnLayout {
            id: destColumn
            x: Theme.gap + 2; y: Theme.gap + 2
            width: parent.width - 2 * Theme.gap - 4
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Rectangle {                            // la spunta: tolta, i pezzi restano in arrivo per un'altra volta
                    implicitWidth: 18; implicitHeight: 18; radius: 4
                    color: dest.item.fare ? Theme.accent : Theme.surface
                    border.width: 1
                    border.color: dest.item.fare ? Theme.accent : Theme.border
                    Text { anchors.centerIn: parent; visible: dest.item.fare; text: "✓"; color: Theme.onAccent; font.pixelSize: 12; font.weight: Font.Bold }
                    MouseArea { anchors.fill: parent; enabled: root.editable; onClicked: Workspace.setPlanField(root.planId, "destinazioni", dest.index, "fare", !dest.item.fare) }
                }
                Rectangle {                            // che cosa è: una lezione nuova (con la data) o un'aggiunta
                    implicitWidth: kind.implicitWidth + 16
                    implicitHeight: 20
                    radius: 10
                    color: dest.item.into === "new" ? Theme.selected : Theme.panel
                    TxLabel { id: kind; anchors.centerIn: parent; font.pixelSize: Theme.smallSize - 1
                              text: dest.item.into !== "new" ? (Workspace.byTopic ? "Si aggiunge a un argomento che hai" : "Si aggiunge a una lezione che hai")
                                  : Workspace.byTopic ? "Argomento nuovo"
                                  : "Lezione nuova" + (dest.item.date ? " · " + root.dateWords(dest.item.date) : " · senza data") }
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                Layout.fillWidth: true
                text: (root.units.findIndex(u => u.key === root.unitKey(dest.item)) + 1) + ". " + root.pretty(dest.item.into === "new" ? dest.item.title : root.nameOf(dest.item.into))
                textFormat: Text.RichText
                color: Theme.text
                font.pixelSize: Theme.textSize + 2
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            TxLabel {
                Layout.fillWidth: true
                visible: !!dest.item.why
                text: root.pretty(dest.item.why || "")
                textFormat: Text.RichText
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Caption { text: "Da" }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: dest.item.concepts.map(root.holderOf).filter((k, i, a) => a.indexOf(k) === i)
                    Chip {
                        required property string modelData
                        key: modelData
                        label: root.nameOf(modelData) + " · " + dest.item.concepts.filter(id => root.holderOf(id) === modelData).length
                               + (dest.item.concepts.filter(id => root.holderOf(id) === modelData).length === 1 ? " concetto" : " concetti")
                    }
                }
            }
            Caption { text: "Concetti" }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: dest.item.concepts
                    ConceptChip { required property string modelData; conceptId: modelData; maxWidth: destColumn.width }
                }
            }
            Caption { visible: root.editable || (dest.item.nota || "") !== ""; text: "Istruzioni" }
            TxTextArea {
                Layout.fillWidth: true
                visible: root.editable || (dest.item.nota || "") !== ""
                readOnly: !root.editable
                minHeight: Theme.rowHeight + 6
                placeholder: "Se vuoi cambiare qualcosa di questa parte, scrivilo qui: il piano si rifà."
                Component.onCompleted: text = dest.item.nota || ""
                onEdited: { Workspace.setPlanField(root.planId, "destinazioni", dest.index, "nota", text); root.instructions = Workspace.planHasInstructions(root.planId) }
            }
        }
    }

    TxScroll {
        id: scroll
        anchors.fill: parent
        contentHeight: column.implicitHeight + 4 * Theme.gap
        ColumnLayout {
            id: column
            x: Theme.gap + 4
            y: Theme.gap + 4
            width: Math.min(root.width - 2 * Theme.gap - 8, 1280)          // usa lo spazio che c'è, fino a un limite di lettura
            readonly property int cards: width >= 1000 ? 2 : 1   // concetti e destinazioni su una o due colonne
            spacing: Theme.gap

            TxLabel { Layout.fillWidth: true; text: root.plan.titolo || "Questo lavoro non c'è più."; font.pixelSize: Theme.textSize + 4; font.weight: Font.DemiBold }
            TxLabel {
                Layout.fillWidth: true
                text: root.plan.stato === "piano" ? "Piano da approvare" : root.plan.stato === "esito" ? "Eseguito" : ""
                color: Theme.warn
                font.pixelSize: Theme.smallSize
            }
            TxLabel { Layout.fillWidth: true; text: root.plan.riepilogo || ""; color: Theme.textMuted; wrapMode: Text.WordWrap; elide: Text.ElideNone }
            TxLabel {
                Layout.fillWidth: true
                text: (root.plan.comuni || []).length + " concetti in comune · "
                      + (root.plan.destinazioni || []).filter(d => d.into === "new").length + " lezioni nuove · "
                      + (root.plan.destinazioni || []).filter(d => d.into !== "new").length + " aggiunte a lezioni che hai · "
                      + (root.plan.gia || []).length + " già nel corso"
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }

            // 1. un corso che non c'è ancora: prima di tutto, in breve, la scaletta che nasce da questo materiale; la si fa
            // cambiare con una riga, o dando (o cambiando) il programma del corso, e il piano si rifà
            Rectangle {
                id: preview
                visible: (root.plan.corso || []).length === 0 && root.units.length > 0
                Layout.fillWidth: true
                implicitHeight: previewColumn.implicitHeight + 2 * Theme.gap + 4
                radius: Theme.radius + 2
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                ColumnLayout {
                    id: previewColumn
                    x: Theme.gap + 2; y: Theme.gap + 2
                    width: parent.width - 2 * Theme.gap - 4
                    spacing: 8
                    TxLabel { Layout.fillWidth: true; text: "La scaletta del corso"; font.pixelSize: Theme.textSize + 2; font.weight: Font.DemiBold }
                    TxLabel {
                        Layout.fillWidth: true
                        text: "Il corso non c'è ancora: da questo materiale nascono " + root.units.length + (Workspace.byTopic ? " argomenti" : " lezioni")
                              + ", in quest'ordine. Sotto, che cosa entra in ciascuno e da dove."
                        color: Theme.textMuted
                        font.pixelSize: Theme.smallSize
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.units.map((u, i) => "<b>" + (i + 1) + ".</b> " + root.pretty(u.title)).join("&nbsp;&nbsp;·&nbsp;&nbsp; ")
                        textFormat: Text.RichText
                        color: Theme.text
                        font.pixelSize: Theme.textSize
                        wrapMode: Text.WordWrap
                        lineHeight: 1.25
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap / 2
                        TxLabel {
                            Layout.fillWidth: true
                            text: Workspace.program !== "" ? "Programma del corso: " + Workspace.program.split("/").pop()
                                                           : "Senza programma del corso: l'ordine è quello più logico per il materiale."
                            color: Theme.textMuted
                            font.pixelSize: Theme.smallSize
                        }
                        TxButton {
                            visible: root.editable
                            text: Workspace.program !== "" ? "Cambia…" : "Dai il programma…"
                            flat: true
                            onClicked: programDialog.open()
                        }
                        TxButton { visible: root.editable && Workspace.program !== ""; text: "Togli"; flat: true; onClicked: Workspace.setProgram("") }
                    }
                    TxTextArea {
                        Layout.fillWidth: true
                        visible: root.editable || (root.plan.nota_scaletta || "") !== ""
                        readOnly: !root.editable
                        minHeight: Theme.rowHeight + 6
                        placeholder: "Vuoi un'altra scaletta? Scrivilo qui (unisci, dividi, sposta, rinomina): il piano si rifà."
                        Component.onCompleted: text = root.plan.nota_scaletta || ""
                        onEdited: { Workspace.setOutlineNote(root.planId, text); root.instructions = Workspace.planHasInstructions(root.planId) }
                    }
                }
            }
            FileDialog {
                id: programDialog
                title: "Il programma, l'indice o la scaletta del corso"
                onAccepted: Workspace.setProgram(Workspace.localPath(selectedFile))
            }

            // 2. dove va il materiale, e dove si interseca
            Heading {
                visible: (root.plan.comuni || []).length > 0 || root.rightKeys.length > 0
                title: "Dove va il materiale"
                detail: column.width >= 560 ? "A sinistra il materiale nuovo, a destra la scaletta: ogni linea dice quanti concetti di un file vanno in quella "
                                              + (Workspace.byTopic ? "parte" : "lezione") + ". Gli archi colorati a sinistra uniscono i file che hanno concetti in comune. Un clic su un nome mostra solo i suoi."
                                            : "Una riga per file nuovo: con chi ha concetti in comune, e quanti. Un clic mostra solo i suoi."
            }
            // poco spazio: al posto del grafo una riga per fonte nuova, con chi interseca e quanti concetti
            Repeater {
                model: column.width < 560 && (root.plan.comuni || []).length > 0 ? root.leftKeys.filter(k => root.sharedHolders.includes(k)) : []
                Rectangle {
                    id: compact
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: compactColumn.implicitHeight + Theme.gap
                    radius: Theme.radius
                    color: root.focusKey === modelData ? Theme.selected : Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    ColumnLayout {
                        id: compactColumn
                        x: Theme.gap / 2; y: Theme.gap / 2
                        width: parent.width - Theme.gap
                        spacing: 2
                        Chip { key: compact.modelData }
                        TxLabel {
                            Layout.fillWidth: true
                            text: Object.keys(root.pairs).filter(p => p.split("|").includes(compact.modelData))
                                  .map(p => { const other = p.split("|").find(x => x !== compact.modelData); const course = root.holders[other] && root.holders[other].course
                                          return root.pairs[p] + " in comune con " + (course ? "la " + root.nameOf(other).charAt(0).toLowerCase() + root.nameOf(other).slice(1) : root.nameOf(other)) })
                                  .join(" · ")
                            color: Theme.textMuted
                            font.pixelSize: Theme.smallSize
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: root.focusKey = root.focusKey === compact.modelData ? "" : compact.modelData }
                }
            }
            Rectangle {
                id: graphBox
                visible: ((root.plan.comuni || []).length > 0 || root.rightKeys.length > 0) && column.width >= 560
                Layout.fillWidth: true
                readonly property int rowH: 30
                readonly property int pad: 26
                implicitHeight: pad * 2 + Math.max(root.leftKeys.length, root.rightKeys.length) * rowH + legend.implicitHeight + Theme.gap
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                // le posizioni dallo spazio che c'è: a sinistra il posto degli archi fra file nuovi (se ce ne sono), in mezzo quello
                // delle linee, ai nomi il resto
                readonly property bool arcs: Object.keys(root.pairs).some(k => k.split("|").every(x => root.leftKeys.includes(x)))
                readonly property real leftX: arcs ? 56 : 12
                readonly property real nodeW: Math.max(110, Math.min(320, (width - leftX - 16 - Math.max(140, width * 0.28)) / 2))
                readonly property real rightX: width - nodeW - 16
                function yOf(key) {
                    const l = root.leftKeys.indexOf(key)
                    if (l >= 0)
                        return pad + l * rowH + rowH / 2
                    const offset = (Math.max(root.leftKeys.length, root.rightKeys.length) - root.rightKeys.length) * rowH / 2
                    return pad + offset + root.rightKeys.indexOf(key) * rowH + rowH / 2
                }
                Canvas {
                    id: edges
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.font = Theme.smallSize + "px sans-serif"
                        const labels = []                  // i numeri già messi: uno non va sopra l'altro
                        const many = Object.keys(root.flows).length > 40   // tante linee: i numeri solo di quelle scelte
                        // le linee verso la scaletta (quanti concetti vanno lì), poi gli archi fra file nuovi (concetti in comune)
                        const lines = Object.keys(root.flows).map(k => [k, root.flows[k], true])
                                      .concat(Object.keys(root.pairs).filter(k => k.split("|").every(x => root.leftKeys.includes(x))).map(k => [k, root.pairs[k], false]))
                        for (const [k, n, flow] of lines) {
                            const [a, b] = k.split("|")
                            const la = root.leftKeys.includes(a), lb = flow ? false : root.leftKeys.includes(b)
                            if (flow && !root.rightKeys.includes(b))
                                continue
                            const on = root.focusKey === "" || a === root.focusKey || b === root.focusKey
                            ctx.globalAlpha = on ? (flow ? 0.6 : 0.75) : 0.1
                            ctx.strokeStyle = flow ? Theme.textFaint : Theme.accent
                            ctx.lineWidth = Math.min(1 + n / 2, 6)
                            const ya = graphBox.yOf(a), yb = graphBox.yOf(b)
                            ctx.beginPath()
                            if (la !== lb) {
                                const [yl, yr] = la ? [ya, yb] : [yb, ya]
                                const x1 = graphBox.leftX + graphBox.nodeW, x2 = graphBox.rightX
                                ctx.moveTo(x1, yl)
                                ctx.bezierCurveTo(x1 + (x2 - x1) * 0.45, yl, x2 - (x2 - x1) * 0.45, yr, x2, yr)
                                ctx.stroke()
                                if (many && (root.focusKey === "" || !on))
                                    continue
                                // il numero sta sulla sua linea: a metà, o più avanti e più indietro se lì c'è già un altro
                                const dx = (x2 - x1) * 0.45
                                const at = t => {
                                    const u = 1 - t
                                    return [u * u * u * x1 + 3 * u * u * t * (x1 + dx) + 3 * u * t * t * (x2 - dx) + t * t * t * x2,
                                            u * u * u * yl + 3 * u * u * t * yl + 3 * u * t * t * yr + t * t * t * yr]
                                }
                                let mx = 0, my = 0
                                for (const t of [0.5, 0.38, 0.62, 0.28, 0.72, 0.2, 0.8]) {
                                    [mx, my] = at(t)
                                    if (!labels.some(p => Math.hypot(p[0] - mx, p[1] - my) < 24))
                                        break
                                }
                                labels.push([mx, my])
                                ctx.globalAlpha = on ? 1 : 0.15
                                ctx.fillStyle = Theme.surface
                                ctx.strokeStyle = Theme.border
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.arc(mx, my, 10, 0, 2 * Math.PI)
                                ctx.fill()
                                ctx.stroke()
                                ctx.fillStyle = Theme.text
                                ctx.textAlign = "center"
                                ctx.fillText(String(n), mx, my + 4)
                            } else {                   // due file nuovi: un arco a sinistra
                                const x = la ? graphBox.leftX : graphBox.rightX + graphBox.nodeW
                                const bulge = Math.min(60, 20 + Math.abs(ya - yb) * 0.3) * (la ? -1 : 1)
                                ctx.moveTo(x, ya)
                                ctx.bezierCurveTo(x + bulge, ya, x + bulge, yb, x, yb)
                                ctx.stroke()
                            }
                        }
                    }
                    Connections {
                        target: root
                        function onPlanChanged() { edges.requestPaint() }
                        function onFocusKeyChanged() { edges.requestPaint() }
                    }
                    Connections { target: Theme; function onChanged() { edges.requestPaint() } }
                    onWidthChanged: requestPaint()
                }
                Repeater {
                    model: root.leftKeys.concat(root.rightKeys)
                    Rectangle {
                        id: node
                        required property string modelData
                        readonly property bool atLeft: root.leftKeys.includes(modelData)
                        readonly property bool lonely: atLeft ? !root.sharedHolders.includes(modelData) && !Object.keys(root.flows).some(k => k.startsWith(modelData + "|"))
                                                              : !Object.keys(root.flows).some(k => k.endsWith("|" + modelData))
                        x: atLeft ? graphBox.leftX : graphBox.rightX
                        y: graphBox.yOf(modelData) - height / 2
                        width: graphBox.nodeW
                        height: 24
                        radius: 12
                        color: root.focusKey === modelData ? Theme.selected : nodeArea.containsMouse ? Theme.hover : Theme.panel
                        border.width: 1
                        border.color: lonely ? Theme.border : root.colorOf(modelData)
                        Rectangle { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: root.colorOf(node.modelData) }
                        TxLabel { id: nodeText; x: 24; width: parent.width - 32; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: Theme.smallSize; color: node.lonely ? Theme.textMuted : Theme.text
                                  text: node.atLeft ? root.nameOf(node.modelData) : (root.units.findIndex(u => u.key === node.modelData) + 1) + ". " + root.nameOf(node.modelData) }
                        LatexTip { text: nodeText.text; visible: nodeArea.containsMouse && nodeText.truncated }
                        MouseArea { id: nodeArea; anchors.fill: parent; hoverEnabled: true; onClicked: root.focusKey = root.focusKey === node.modelData ? "" : node.modelData }
                    }
                }
                TxLabel { x: graphBox.leftX; y: 6; text: "MATERIALE NUOVO"; color: Theme.textMuted; font.pixelSize: Theme.smallSize - 1; font.letterSpacing: 0.6 }
                TxLabel { x: graphBox.rightX; y: 6; text: "LA SCALETTA"; color: Theme.textMuted; font.pixelSize: Theme.smallSize - 1; font.letterSpacing: 0.6 }
                Flow {
                    id: legend
                    x: Theme.gap
                    y: parent.height - implicitHeight - Theme.gap / 2
                    width: parent.width - 2 * Theme.gap
                    spacing: Theme.gap
                    Repeater {
                        model: root.fileColors
                        Row {
                            required property var modelData
                            spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: parent.modelData.color }
                            TxLabel { text: parent.modelData.file; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                        }
                    }
                    Row {
                        visible: root.rightKeys.some(k => !k.startsWith("new:"))
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: Theme.textMuted }
                        TxLabel { text: Workspace.byTopic ? "i tuoi argomenti" : "le tue lezioni"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    }
                    Row {
                        visible: root.rightKeys.some(k => k.startsWith("new:"))
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                        TxLabel { text: Workspace.byTopic ? "argomenti nuovi" : "lezioni nuove"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    }
                }
            }
            RowLayout {
                visible: root.focusKey !== ""
                Layout.fillWidth: true
                TxLabel { Layout.fillWidth: true; text: "Solo ciò che riguarda " + root.nameOf(root.focusKey); color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                TxButton { text: "Mostra tutti"; flat: true; onClicked: root.focusKey = "" }
            }
            RowLayout {                        // due colonne quando c'è spazio, ognuna scorre per conto suo: niente buchi fra le righe
                Layout.fillWidth: true
                spacing: Theme.gap
                Repeater {
                    model: column.cards
                    ColumnLayout {
                        id: pile
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.gap
                        Repeater {
                            model: (root.plan.comuni || []).map((g, i) => ({ g: g, i: i })).filter(e => e.i % column.cards === pile.index)
                            Rectangle {
                                id: shared
                                required property var modelData
                                readonly property var group: modelData.g
                                readonly property int index: modelData.i
                                // dove va la versione sola, detto corto: la lezione della scaletta («nella lezione 5», «in «Le emozioni»»),
                                // non la fonte da cui viene
                                readonly property string unit: root.unitOf(group.ospite)
                                readonly property string host: {
                                    if (unit === "")
                                        return "in " + root.nameOf(root.holderOf(group.ospite))
                                    if (unit.startsWith("new:"))
                                        return "in «" + unit.slice(4) + "»"
                                    const [head, ...rest] = root.nameOf(unit).split(" · ")
                                    return (head.startsWith("Argomento") ? "nell'" : "nella ") + head.toLowerCase() + (rest.length ? " (" + rest.join(" · ") + ")" : "")
                                }
                                property bool open: false
                                visible: root.focusKey === "" || shared.unit === root.focusKey || group.items.some(id => root.holderOf(id) === root.focusKey)
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: sharedColumn.implicitHeight + 2 * Theme.gap + 4
                                radius: Theme.radius + 2
                                color: Theme.surface
                                border.width: 1
                                border.color: Theme.border
                                ColumnLayout {
                                    id: sharedColumn
                                    x: Theme.gap + 2; y: Theme.gap + 2
                                    width: parent.width - 2 * Theme.gap - 4
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.pretty(shared.group.name)
                                        textFormat: Text.RichText
                                        color: Theme.text
                                        font.pixelSize: Theme.textSize + 1
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                    }
                                    Caption { text: "Dove compare" }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Repeater {
                                            // un nome per fonte: più concetti della stessa lezione sono un nome solo, con il conto
                                            model: shared.group.items.map(root.holderOf).filter((k, i, a) => a.indexOf(k) === i)
                                            Chip {
                                                required property string modelData
                                                readonly property var mine: shared.group.items.filter(id => root.holderOf(id) === modelData)
                                                key: modelData
                                                label: root.nameOf(modelData) + (mine.length > 1 ? " · " + mine.length + " punti" : "")
                                                tip: mine.map(root.whatOf).join("\n\n")
                                            }
                                        }
                                    }
                                    Caption { visible: (shared.group.note || "") !== ""; text: "Che cosa cambia" }
                                    TxRendered { visible: (shared.group.note || "") !== ""; text: shared.group.note || ""; textColor: Theme.text; zoom: 0.5 }
                                    Caption { text: "Che cosa fare" }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: Theme.gap / 2
                                        Repeater {
                                            model: [["one", "Una versione sola, " + shared.host], ["both", "Tienile tutte"], ["skip", "Salta il nuovo"]]
                                                    .filter(c => c[0] !== "skip" || shared.group.items.some(id => root.holders[root.holderOf(id)] && root.holders[root.holderOf(id)].course))
                                            TxButton {
                                                required property var modelData
                                                readonly property bool on: shared.group.valore === modelData[0]
                                                text: (on ? "✓ " : "") + modelData[1]
                                                primary: on
                                                enabled: root.editable
                                                width: Math.min(implicitWidth, sharedColumn.width)
                                                onClicked: Workspace.setPlanField(root.planId, "comuni", shared.index, "valore", modelData[0])
                                            }
                                        }
                                    }
                                    TxButton { text: shared.open ? "Nascondi che cosa dice ciascuno" : "Che cosa dice ciascuno"; flat: true; onClicked: shared.open = !shared.open }
                                    Repeater {
                                        model: shared.open ? shared.group.items : []
                                        Rectangle {                    // una fonte, con che cosa dice: compilato come nel PDF
                                            id: said0
                                            required property string modelData
                                            Layout.fillWidth: true
                                            implicitHeight: said.implicitHeight + 16
                                            radius: Theme.radius
                                            color: Theme.panel
                                            Rectangle { x: 0; y: 6; width: 3; height: parent.height - 12; radius: 1.5; color: root.colorOf(root.holderOf(said0.modelData)) }
                                            ColumnLayout {
                                                id: said
                                                x: 12; y: 8
                                                width: parent.width - 20
                                                spacing: 4
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: root.nameOf(root.holderOf(said0.modelData)) + " · " + root.pretty(root.titleOf(said0.modelData))
                                                    textFormat: Text.RichText
                                                    color: Theme.textMuted
                                                    font.pixelSize: Theme.smallSize
                                                    wrapMode: Text.WordWrap
                                                }
                                                TxRendered { text: root.whatOf(said0.modelData); zoom: 0.48 }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 2. ciò che resta di chi si interseca
            Heading {
                visible: (root.plan.destinazioni || []).some(d => root.touches(d))
                title: "Ciò che resta di questi file"
                detail: "Tolti i concetti in comune (qui sbiaditi: li decidi sopra), dove va il resto. Una lezione nuova può prendere concetti da più file."
            }
            ColumnLayout {                     // una colonna: i riquadri hanno altezze diverse, su due resterebbero buchi
                Layout.fillWidth: true
                spacing: Theme.gap
                Repeater {
                    model: (root.plan.destinazioni || []).map((d, i) => ({ d: d, i: i })).filter(e => root.touches(e.d))
                    Destination { required property var modelData; item: modelData.d; index: modelData.i }
                }
            }

            // 3. tutto nuovo
            Heading {
                visible: (root.plan.destinazioni || []).some(d => !root.touches(d))
                title: "Tutto nuovo"
                detail: "Materiale che non si interseca con niente: entra così com'è."
            }
            ColumnLayout {                     // una colonna: i riquadri hanno altezze diverse, su due resterebbero buchi
                Layout.fillWidth: true
                spacing: Theme.gap
                Repeater {
                    model: (root.plan.destinazioni || []).map((d, i) => ({ d: d, i: i })).filter(e => !root.touches(e.d))
                    Destination { required property var modelData; item: modelData.d; index: modelData.i }
                }
            }

            // l'ordine degli argomenti, se il modello ne propone uno più logico: com'è e come diventa
            Heading {
                visible: ((root.plan.ordine || {}).proposta || []).length > 0
                title: "Ordine degli argomenti"
                detail: "Un ordine più logico, con gli argomenti nuovi al loro posto. Si applica alla fine del lavoro; il contenuto non cambia."
            }
            RowLayout {
                visible: ((root.plan.ordine || {}).proposta || []).length > 0
                Layout.fillWidth: true
                spacing: Theme.gap * 2
                Rectangle {                            // la spunta
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 18; implicitHeight: 18; radius: 4
                    readonly property bool on: (root.plan.ordine || {}).valore === true
                    color: on ? Theme.accent : Theme.surface
                    border.width: 1
                    border.color: on ? Theme.accent : Theme.border
                    Text { anchors.centerIn: parent; visible: parent.on; text: "✓"; color: Theme.onAccent; font.pixelSize: 12; font.weight: Font.Bold }
                    MouseArea { anchors.fill: parent; enabled: root.editable; onClicked: Workspace.setPlanOrder(root.planId, !parent.on) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 4
                    TxLabel { Layout.fillWidth: true; text: "Com'è"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    Repeater {
                        model: root.plan.corso || []
                        TxLabel { required property var modelData; required property int index; Layout.fillWidth: true
                                  text: (index + 1) + ". " + root.pretty(String(modelData.name).split(" · ").slice(1).join(" · ")); textFormat: Text.RichText }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 4
                    TxLabel { Layout.fillWidth: true; text: "Come diventa"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    Repeater {
                        model: (root.plan.ordine || {}).proposta || []
                        TxLabel { required property var modelData; required property int index; Layout.fillWidth: true
                                  text: (index + 1) + ". " + root.pretty(modelData.title) + (String(modelData.key).startsWith("new:") ? " <font color=\"" + Theme.textMuted + "\">· nuovo</font>" : "")
                                  textFormat: Text.RichText }
                    }
                }
            }

            // versioni nuove di fonti usate
            Repeater {
                model: root.plan.versioni || []
                ColumnLayout {
                    id: version
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.gap
                    spacing: 6
                    TxLabel { Layout.fillWidth: true; text: version.modelData.titolo; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.gap / 2
                        Repeater {
                            model: [["porta", "Porta le differenze"], ["nuovo", "Smistala come materiale nuovo"], ["salta", "Salta"]]
                            TxButton {
                                required property var modelData
                                readonly property bool on: version.modelData.valore === modelData[0]
                                text: (on ? "✓ " : "") + modelData[1]
                                primary: on
                                enabled: root.editable
                                onClicked: Workspace.setPlanField(root.planId, "versioni", version.index, "valore", modelData[0])
                            }
                        }
                    }
                }
            }

            // ciò che è già nel corso, ciò che si salta, ciò che non si legge: una riga che si apre. Una parte da saltare si
            // può tenere («Non saltarla»): è un'istruzione, e il piano si rivede
            ColumnLayout {
                id: known
                property bool open: false
                // gli elenchi che vengono dal C++ non sono array JavaScript: Array.from prima di concat
                readonly property var skipped: Array.from(root.plan.salta || []).reduce((all, s, index) => all.concat(Array.from(s.concepts).map(id => ({
                    index: index, id: id, kept: Array.from(s.tieni || []).includes(id),
                    text: "«" + root.titleOf(id) + "» (" + root.nameOf(root.holderOf(id)) + "): " + s.why }))), [])
                readonly property int keptCount: skipped.filter(s => s.kept).length
                readonly property var lines: Array.from(root.plan.gia || []).map(t => ({ text: t }))
                    .concat(skipped)
                    .concat(Array.from(root.plan.illeggibili || []).map(u => ({ text: "Non so leggere " + u })))
                visible: lines.length > 0
                Layout.fillWidth: true
                Layout.topMargin: Theme.gap
                spacing: 4
                TxButton {
                    text: (known.open ? "▾ " : "▸ ") + (root.plan.gia || []).length + " già nel corso, " + (known.skipped.length - known.keptCount)
                          + (known.skipped.length - known.keptCount === 1 ? " parte" : " parti") + " da saltare" + (known.keptCount ? ", " + known.keptCount + " da tenere" : "")
                          + ((root.plan.illeggibili || []).length ? ", " + root.plan.illeggibili.length + " file che non so leggere" : "")
                    flat: true
                    onClicked: known.open = !known.open
                }
                Repeater {
                    model: known.open ? known.lines : []
                    RowLayout {
                        id: knownLine
                        required property var modelData
                        readonly property bool skip: modelData.id !== undefined
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        TxLabel {
                            Layout.fillWidth: true
                            text: knownLine.skip ? (knownLine.modelData.kept ? "Non lo salto: " : "Salto ") + knownLine.modelData.text : knownLine.modelData.text
                            color: knownLine.skip && knownLine.modelData.kept ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.smallSize
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                        TxButton {
                            visible: knownLine.skip && root.editable
                            text: knownLine.modelData.kept ? "Saltala" : "Non saltarla"
                            flat: true
                            hint: knownLine.modelData.kept ? "Torna come l'aveva proposta il modello" : "La parte entra nel corso: «Rivedi piano» le trova un posto"
                            onClicked: {
                                const s = Array.from(root.plan.salta)[knownLine.modelData.index]
                                const kept = Array.from(s.tieni || []).filter(id => id !== knownLine.modelData.id)
                                Workspace.setPlanField(root.planId, "salta", knownLine.modelData.index, "tieni",
                                                       knownLine.modelData.kept ? kept : kept.concat([knownLine.modelData.id]))
                            }
                        }
                    }
                }
            }

            // come si scrive e quanto costa
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gap
                visible: root.editable
                spacing: Theme.gap
                TxLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    color: Theme.textMuted
                    font.pixelSize: Theme.smallSize
                    text: (root.plan.impostazioni || "") + (root.plan.stima >= 0.01 ? "\nCosto stimato per scrivere le lezioni nuove: circa "
                          + Number(root.plan.stima).toLocaleString(Qt.locale("it_IT"), "f", 2) + " $, più il lavoro dell'agente dove si fonde." : "")
                          + (root.plan.costo >= 0.01 ? "\nQuesta mappa è costata " + Number(root.plan.costo).toLocaleString(Qt.locale("it_IT"), "f", 2) + " $." : "")
                }
                TxButton { text: "Cambia"; hint: "Le impostazioni del progetto (⌘,)"; onClicked: Workspace.askSettings() }
            }
            TxLabel { Layout.fillWidth: true; visible: root.editable; text: "Commento"; font.weight: Font.Medium }
            TxTextArea {
                Layout.fillWidth: true
                visible: root.editable
                placeholder: "Che cosa cambieresti nel piano?"
                Component.onCompleted: text = root.plan.commento || ""
                onEdited: { Workspace.setPlanComment(root.planId, text); root.instructions = Workspace.planHasInstructions(root.planId) }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gap
                spacing: Theme.gap
                TxButton {
                    visible: root.plan.stato !== "esito" && root.plan.titolo !== undefined
                    text: root.confirmDiscard ? "Sì, butta il piano" : "Butta il piano"
                    onClicked: {
                        if (!root.confirmDiscard)
                            return root.confirmDiscard = true
                        Workspace.discardPlan(root.planId)
                        root.closeAsked()
                    }
                }
                TxLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    color: Theme.warn
                    font.pixelSize: Theme.smallSize
                    text: root.editable && Workspace.busy ? "Aspetta che finisca il lavoro in corso" : ""
                }
                TxButton {
                    visible: root.editable
                    text: root.instructions ? "Rivedi piano" : "Avvia"
                    primary: true
                    enabled: !Workspace.busy
                    hint: Workspace.busy ? "Aspetta che finisca il lavoro in corso" : root.instructions ? "Rifà la mappa con le tue istruzioni" : ""
                    onClicked: {
                        if (root.instructions)
                            Workspace.reviseSortPlan(root.planId)
                        else
                            Workspace.startSortPlan(root.planId)
                        root.closeAsked()
                    }
                }
            }
        }
    }
}
