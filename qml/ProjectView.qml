import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Il progetto aperto: la barra in alto, il pannello Progetto, l'editor con il PDF accanto, gli avvisi, la barra di stato.
Item {
    id: root
    property bool leftOpen: true
    property bool bottomOpen: false
    property bool chatOpen: false
    property int leftTab: 0
    signal convertAsked()
    signal newLessonAsked()
    signal materialAsked()
    signal sourcesAsked(string lesson)
    signal newVersionAsked(string used)
    signal renameAsked(string file)
    // Rivedi impaginazione: apre il riepilogo; le lezioni nuove si impaginano dal suo tasto, non aprendolo.
    function reviewLayout() { root.openFile("rivedi:impaginazione") }
    function saveAll() { editor.saveAll() }

    function openFile(file, line) { editor.openFile(file, line || 0) }
    function showConsole() { root.bottomOpen = true; bottom.tab = 1 }
    // ⌘L: la selezione dell'editor entra nel messaggio per l'agente.
    function quoteSelection() {
        const s = editor.selection()
        if (!s)
            return Workspace.say("Apri prima un file di testo.")
        root.chatOpen = true
        chat.quote(s.file, s.from, s.to, s.text)
    }
    // Una foto di un pezzo della finestra, allegata al prossimo messaggio.
    function shot(what) {
        const item = what === "editor" ? editor : what === "pdf" ? pdf : root
        item.grabToImage(result => {
            const path = Chat.shotPath()
            if (result.saveToFile(path))
                chat.attach([path])
        })
    }
    function save() { editor.saveAll() }
    // ▶ File: la lezione aperta da sola; un altro file, o nessuno, il documento.
    // Risolvere a mano un problema di impaginazione: la lezione al blocco, accanto alla sua prova compilata (la prima si
    // compila da sola; ⌘B le successive; ⌘-clic sulla pagina porta alla riga, ⌘J al punto del PDF). Progetto e chat si
    // chiudono, e tornano alla fine se c'erano.
    property var manual: null                          // lesson, block, what, leftOpen, chatOpen
    property var manualSpot: ({ line: 1, present: false, movable: false })   // dov'è la mossa, e se la sua riga c'è
    function refreshManual() { if (manual) manualSpot = Workspace.moveSpot(manual.lesson, manual.block) }
    function startManual(lesson, block, line, what) {
        const base = reviewSaved || { leftOpen: leftOpen, chatOpen: chatOpen }
        editor.saveAll()
        // com'erano la lezione e il registro: «Chiudi senza applicare» li rimette
        manual = { lesson: lesson, block: block, what: what, leftOpen: base.leftOpen, chatOpen: base.chatOpen,
                   original: Workspace.read(lesson), registry: Workspace.read("salvataggi/impaginazione.json") }
        Workspace.decideMove(lesson, block, "a_mano", false)
        refreshManual()
        leftOpen = false
        chatOpen = false
        bottomOpen = false
        split = 1
        openFile(lesson, manualSpot.line)
        compileFile()
    }
    // «Vai alla mossa»: nell'editor e nella prova.
    function goToMove() {
        refreshManual()
        openFile(manual.lesson, manualSpot.line)
        jumpToPdf(manual.lesson, manualSpot.line)
    }
    // Con o senza la mossa fatta dal programma: si scambia, e si va a vedere quel punto.
    function setMove(on) {
        editor.saveAll()
        Workspace.decideMove(manual.lesson, manual.block, on ? "accettata" : "lasciata", false)
        Workspace.decideMove(manual.lesson, manual.block, "a_mano", false)
        refreshManual()
        openFile(manual.lesson, manualSpot.line)
        compileFile()
    }
    function endManual(solved) {
        if (!manual)
            return
        const m = manual
        manual = null
        editor.saveAll()
        if (solved) {
            Workspace.keepManual(m.lesson, m.block, m.original, m.registry)   // la × sulla mossa riporta qui
            Workspace.decideMove(m.lesson, m.block, "risolta")   // applicata: il seguito della lezione si rimpagina
        } else {
            Workspace.restoreManual(m.lesson, m.original, m.registry)   // le prove non restano
        }
        if (!Workspace.jobRunning)
            Workspace.compileDocument()                // la prova era della lezione sola: si torna al documento
        if (reviewSaved) {                             // si torna alla revisione, che sta a tutto schermo
            split = 2
        } else {
            leftOpen = m.leftOpen
            chatOpen = m.chatOpen
        }
        openFile("rivedi:impaginazione")
    }
    // Le revisioni (impaginazione, dubbi, correzioni, figure) prendono la finestra: i pannelli si chiudono, e tornano
    // com'erano quando le si lascia. Rivedi figure tiene l'anteprima accanto: «Vai alla figura» la mostra lì.
    property var reviewSaved: null
    function syncReview() {
        if (manual)
            return
        const file = editor.currentFile
        const inReview = ["rivedi:impaginazione", "rivedi:dubbi", "rivedi:correzioni", "rivedi:figure"].includes(file)
        if (inReview) {
            if (!reviewSaved)
                reviewSaved = { leftOpen: leftOpen, chatOpen: chatOpen, bottomOpen: bottomOpen, split: split }
            leftOpen = false
            chatOpen = false
            bottomOpen = false
            split = file === "rivedi:figure" ? 1 : 2
        } else if (reviewSaved) {
            const s = reviewSaved
            reviewSaved = null
            leftOpen = s.leftOpen
            chatOpen = s.chatOpen
            bottomOpen = s.bottomOpen
            split = s.split === 2 ? 1 : s.split
        }
    }
    function compileFile() {
        if (Workspace.compiling)
            return Workspace.say("Sta già compilando.")
        editor.saveAll()
        if (editor.currentFile === "" || editor.kindOf(editor.currentFile) !== "text")
            return Workspace.say("Apri prima una lezione.")
        pendingJump = { file: editor.currentFile, line: editor.currentLine }   // il PDF si apre dove si stava scrivendo
        Workspace.compileFile(editor.currentFile)
    }
    function compileDocument() {
        if (Workspace.compiling)
            return Workspace.say("Sta già compilando.")
        editor.saveAll()
        Workspace.compileDocument()
    }
    function toggleWrap() { editor.toggleWrap() }

    // ⌘Z: prima il testo dell'editor, finché la sua pila ne ha; poi la cronologia del progetto.
    function undo() {
        if (editor.textUndo())
            return
        if (Workspace.compiling)
            return Workspace.say("Aspetta la fine della compilazione.")
        editor.saveAll()
        Workspace.undo()
    }
    function redo() {
        if (editor.textRedo())
            return
        if (Workspace.compiling)
            return Workspace.say("Aspetta la fine della compilazione.")
        editor.saveAll()
        Workspace.redo()
    }
    function goToStep(index) {
        editor.saveAll()
        Workspace.goToStep(index)
    }

    // Il salto dal sorgente al PDF (⌘J, ⌘-clic su una riga): se il PDF mostrato non ha quella riga o è più vecchio
    // del file, prima si compila.
    property var pendingJump: null
    function jumpToCursor() { jumpToPdf(editor.currentFile, editor.currentLine) }
    function jumpToPdf(file, line) {
        if (file === "" || editor.kindOf(file) !== "text")
            return Workspace.say("Apri prima una lezione.")
        if (Workspace.compiling)
            return Workspace.say("Sta già compilando.")
        editor.saveAll()
        const lessonOnly = manual !== null && !Workspace.showsLesson(file)   // la prova è della lezione sola
        const spot = lessonOnly ? ({ ok: false, stale: true }) : Workspace.toPdf(file, line)
        if (spot.ok)
            return showSpot(spot)
        if (!spot.stale)
            return Workspace.say("Questa riga non lascia un segno nel PDF: prova con una riga di testo vicina.")
        pendingJump = { file: file, line: line }
        Workspace.compileFile(file)
    }
    function showSpot(spot) {
        if (split === 2)
            split = 1                                  // il PDF era chiuso: torna accanto
        pdf.show(spot.page, spot.y, spot.height)
    }
    // Il salto dal PDF al sorgente (⌘-clic o doppio clic su un punto).
    function jumpToSource(page, x, y) {
        const spot = Workspace.toSource(page, x, y)
        if (!spot.ok)
            return Workspace.say("Non trovo la riga di questo punto.")
        if (split === 0)
            split = 1
        root.openFile(spot.file, spot.line)
    }

    // Il divisore fra editor e PDF ha tre posizioni: 0 PDF pieno, 1 alla pari, 2 editor pieno; -1 dove l'ha lasciato l'utente.
    property int split: 1
    onSplitChanged: applySplit()
    // Nella modifica a mano, con poco spazio, la prova va sotto l'editor.
    readonly property bool stacked: manual !== null && width < 900
    onStackedChanged: Qt.callLater(applySplit)
    function applySplit() {
        if (stacked) {
            editor.SplitView.preferredHeight = sideBySide.height / 2
            return
        }
        if (split === 0)
            editor.SplitView.preferredWidth = 0
        else if (split === 1)
            editor.SplitView.preferredWidth = sideBySide.width / 2
        else if (split === 2)
            editor.SplitView.preferredWidth = sideBySide.width
    }

    Connections {
        target: Workspace
        function onProjectChanged() { editor.closeAll() }
        function onPdfRebuilt() { pdf.reload() }
        function onPlanReady(id) { root.openFile("piano:" + id) }
        function onFileRenamed(from, to) { editor.fileRenamed(from, to) }
        function onAgentAsked(message, title) { root.chatOpen = true; Chat.startProcedure(title, message) }
        function onFilesChanged() { root.refreshManual() }
        function onBuildFinished() {
            if (Workspace.errorCount > 0)
                root.bottomOpen = true
            const jump = root.pendingJump
            root.pendingJump = null
            if (jump)
                Qt.callLater(() => {               // dopo che il PDF nuovo è stato letto
                    const spot = Workspace.toPdf(jump.file, jump.line)
                    if (spot.ok)
                        root.showSpot(spot)
                })
        }
        function onGesture(name, argument) {
            if (name === "apri") {
                const parts = argument.split(" ")
                root.openFile(parts[0], parts.length > 1 ? parseInt(parts[1]) : 0)
            } else if (name === "compila") {
                root.compileFile()
            } else if (name === "tutto") {
                root.compileDocument()
            } else if (name === "salta") {
                root.jumpToPdf(editor.currentFile, editor.currentLine)
            } else if (name === "sorgente") {
                const p = argument.split(" ")
                root.jumpToSource(parseInt(p[0]), parseFloat(p[1]), parseFloat(p[2]))
            } else if (name === "divisore") {
                root.split = parseInt(argument)
            } else if (name === "annulla") {
                root.undo()
            } else if (name === "ripeti") {
                root.redo()
            } else if (name === "passo") {
                Workspace.checkpoint(argument)
            } else if (name === "scrivi") {
                Workspace.save(argument.split(" ")[0], argument.substring(argument.indexOf(" ") + 1))
            } else if (name === "fatta") {
                Workspace.setDone(argument, true)
            } else if (name === "scegli") {
                const a = argument.split(" ")
                Workspace.chooseReading(a[0], a[1], parseInt(a[2]))
            } else if (name === "ritaglio") {             // apre il ritaglio di una figura in Rivedi figure
                editor.cropFigure(argument)
            } else if (name === "figura") {
                Workspace.redoFigure(argument.split(" ")[0], argument.split(" ")[1])
            } else if (name === "risolvi") {
                Workspace.fixAll()
            } else if (name === "impagina") {
                Workspace.paginate()
            } else if (name === "chiedi") {
                root.chatOpen = true
                Chat.send(argument)
            } else if (name === "modo") {
                Chat.mode = argument
            } else if (name === "rispondi") {
                Chat.answer(argument === "si")
            } else if (name === "lezione") {
                const a = argument.split(" ")
                Workspace.say(Workspace.createLesson(parseInt(a[0]), a[1], a.slice(2)))
            } else if (name === "nuova-lezione") {
                root.newLessonAsked()
            } else if (name === "materiale") {
                Workspace.sortMaterial(Workspace.addSources(argument.split(" ")))
            } else if (name === "avvia-materiale") {
                if (Workspace.jobs.length > 0)
                    Workspace.startSortPlan(Workspace.jobs[0].id)
            } else if (name === "versioni") {
                root.leftTab = 1
            } else if (name === "rinomina") {
                Workspace.say(Workspace.renameFile(argument.split(" ")[0], argument.split(" ")[1]))
            } else if (name === "attivita") {
                root.bottomOpen = true
                bottom.tab = 2
            } else if (name === "console") {
                root.bottomOpen = true
                bottom.tab = 1
            } else if (name === "correggo") {
                Workspace.correctDoubt(argument.split(" ")[0], argument.split(" ")[1])
            } else if (name === "decidi") {
                Workspace.decideDoubts(argument.split(" ")[0], argument.split(" ").slice(1))
            } else if (name === "tag") {
                Workspace.showTags = argument !== "no"
            } else if (name === "ramo") {
                const all = Object.assign({}, tree.openLessons)
                all[argument] = true
                tree.openLessons = all
            } else if (name === "pannello") {
                root.openFile(argument.split(" ")[0])
                editor.showLesson(argument.split(" ")[1])
            } else if (name === "cita") {
                root.quoteSelection()
            } else if (name === "allega") {
                root.chatOpen = true
                chat.attach([argument])
            } else if (name === "foto") {
                root.chatOpen = true
                root.shot(argument)
            } else if (name === "invia") {
                chat.sendNow()
            } else if (name === "storico") {
                root.chatOpen = true
                chat.showHistory()
            } else if (name === "chat") {
                root.chatOpen = true
            } else if (name === "miniature") {
                editor.showShots()
            } else if (name === "elimina-impaginazioni") {
                Workspace.resetLayout()
            } else if (name === "mossa") {                // decide la mossa attuale della prima lezione che ne ha una
                const now = Workspace.layoutItems().find(i => i.type === "move" && i.role === "attuale")
                if (now)
                    Workspace.decideMove(now.lesson, now.block, argument)
            } else if (name === "annulla-mossa") {
                const row = Workspace.layoutItems().find(i => i.type === "move" && i.undo !== "")
                if (row)
                    Workspace.reopenMove(row.lesson, row.undo)
            } else if (name === "a-mano") {
                const moves = Workspace.layoutItems().filter(i => i.type === "move")
                if (moves.length > 0) {
                    root.startManual(moves[0].lesson, moves[0].block, moves[0].line, moves[0].what)
                }
            } else if (name === "chiudi-scheda") {
                editor.closeTab(editor.current)
            } else if (name === "inserisci") {
                editor.insertLine(argument)
            } else if (name === "risolta") {
                root.endManual(true)
            } else if (name === "avvia-piano") {
                if (Workspace.jobs.length > 0) {
                    root.chatOpen = true
                    Chat.send(Workspace.planMessage(Workspace.jobs[0].id, "avvia"))
                }
            } else if (name === "archivia") {
                if (Workspace.jobs.length > 0)
                    Workspace.archivePlan(Workspace.jobs[0].id)
            } else if (name === "avvisi") {
                root.bottomOpen = true
            } else if (name === "progetto") {
                root.leftOpen = argument !== "chiuso"
            }
        }
    }

    component Handle: Rectangle {
        id: grip
        implicitWidth: 1
        implicitHeight: 1
        color: Theme.border
        // la presa è più larga della riga che si vede
        containmentMask: Item { x: -3; y: -3; width: grip.width + 6; height: grip.height + 6 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // la barra in alto: fissi solo i tasti di ciò che c'è sempre da fare
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight + 12
            color: Theme.window
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gap
                anchors.rightMargin: Theme.gap
                spacing: Theme.gap
                TxButton {
                    text: "☰"
                    flat: true
                    implicitWidth: 30
                    hint: root.leftOpen ? "Chiudi il pannello Progetto" : "Apri il pannello Progetto"
                    onClicked: root.leftOpen = !root.leftOpen
                }
                TxButton { text: "▶  File"; hint: "Compila il file aperto (⌘B)"; enabled: !Workspace.compiling; onClicked: root.compileFile() }
                TxButton { text: "▶▶  Tutto"; hint: Workspace.jobRunning ? "Un lavoro sta usando il documento: aspetta che finisca" : "Compila tutto il documento (⇧⌘B)"; enabled: !Workspace.compiling && !Workspace.jobRunning; onClicked: root.compileDocument() }
                TxButton { text: "Converti"; hint: "Le fonti da convertire diventano lezioni (⇧⌘K)"; enabled: !Workspace.jobRunning; onClicked: root.convertAsked() }
                // i «Rivedi» ci sono solo quando c'è qualcosa da decidere
                TxButton { visible: Workspace.openDoubts > 0; text: "Rivedi dubbi  " + Workspace.openDoubts; hint: "⇧⌘D"; onClicked: root.openFile("rivedi:dubbi") }
                TxButton { visible: Workspace.openDeclared > 0; text: "Rivedi correzioni  " + Workspace.openDeclared; hint: "⇧⌘E"; onClicked: root.openFile("rivedi:correzioni") }
                TxButton { visible: Workspace.openFigures > 0; text: "Rivedi figure  " + Workspace.openFigures; hint: "⇧⌘F"; onClicked: root.openFile("rivedi:figure") }
                TxButton { visible: Workspace.openLayout > 0; text: "Rivedi impaginazione  " + Workspace.openLayout; hint: "⇧⌘R"; onClicked: root.reviewLayout() }
                Item { Layout.fillWidth: true }
                TxLabel {
                    Layout.maximumWidth: Math.max(60, root.width / 3)
                    text: Workspace.name
                    color: Theme.textMuted
                }
                TxButton { visible: !root.chatOpen; text: "Agente"; hint: "Apri la chat con l'agente"; onClicked: root.chatOpen = true }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        // la striscia della modifica a mano: che cosa c'è da sistemare, e come si esce
        Rectangle {
            Layout.fillWidth: true
            visible: root.manual !== null
            implicitHeight: visible ? manualRow.implicitHeight + 2 * Theme.gap : 0
            color: Theme.selected
            ColumnLayout {
                id: manualRow
                anchors.fill: parent
                anchors.margins: Theme.gap
                spacing: Theme.gap
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxLabel {
                        Layout.fillWidth: true
                        text: root.manual ? "Impaginazione a mano · " + root.manual.what : ""
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        maximumLineCount: 3
                    }
                    TxButton { text: "Chiudi senza applicare"; hint: "La lezione torna com'era prima delle prove"; onClicked: root.endManual(false) }
                    TxButton { text: "Applica alla lezione"; primary: true; hint: "Le prove diventano il testo della lezione: il seguito si rimpagina"; onClicked: root.endManual(true) }
                }
                // a sinistra: andare alla mossa, con o senza; in mezzo i comandi pronti; a destra la prova
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    TxButton { text: "Vai alla mossa"; hint: "Porta l'editor e la prova al punto da sistemare"; onClicked: root.goToMove() }
                    TxSegmented {
                        visible: root.manualSpot.movable === true
                        implicitHeight: Theme.rowHeight + 2
                        implicitWidth: 250
                        choices: [{ value: "con", label: "Con la mossa" }, { value: "senza", label: "Senza la mossa" }]
                        value: root.manualSpot.present ? "con" : "senza"
                        onChosen: value => root.setMove(value === "con")
                    }
                    Rectangle { implicitWidth: 1; implicitHeight: Theme.rowHeight - 6; color: Theme.border }
                    TxLabel { text: "Inserisci:"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    Repeater {
                        model: ["\\newpage", Workspace.breakCommand, "\\vspace{-2mm}"]
                        TxButton {
                            required property string modelData
                            visible: modelData !== ""
                            text: modelData
                            hint: "Inserisce alla riga del cursore"
                            onClicked: { root.openFile(root.manual.lesson, 0); editor.insertLine(modelData) }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    TxLabel {
                        text: root.manual ? "solo " + root.manual.lesson.substring(root.manual.lesson.lastIndexOf("/") + 1) + " · ⌘J: dal cursore alla prova" : ""
                        color: Theme.textMuted
                        font.pixelSize: Theme.smallSize
                    }
                    TxButton {
                        text: Workspace.compiling ? "Compilo…" : "Prova"
                        primary: true
                        hint: "Compila solo questa lezione e mostra com'è venuta (⌘B)"
                        enabled: !Workspace.compiling
                        onClicked: { root.openFile(root.manual.lesson, 0); root.compileFile() }
                    }
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Handle {}

            // a sinistra un pannello solo, con due linguette: Progetto e Version control
            ColumnLayout {
                visible: root.leftOpen
                SplitView.preferredWidth: 270
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: Math.max(200, root.width / 2)
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.rowHeight + 4
                    color: Theme.window
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: ["Progetto", "Version control" + (Workspace.changeCount > 0 ? "  " + Workspace.changeCount : "")]
                            Rectangle {
                                required property string modelData
                                required property int index
                                width: parent.width / 2
                                height: parent.height
                                color: root.leftTab === index ? Theme.panel : "transparent"
                                TxLabel { anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; horizontalAlignment: Text.AlignHCenter; text: modelData; color: root.leftTab === index ? Theme.text : Theme.textMuted; font.pixelSize: Theme.smallSize + 1 }
                                MouseArea { anchors.fill: parent; onClicked: root.leftTab = index }
                            }
                        }
                    }
                }
                GitPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.leftTab === 1
                    onOpened: what => root.openFile(what)
                }
            ProjectPanel {
                id: tree
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.leftTab === 0
                currentFile: editor.currentFile
                onFileChosen: file => root.openFile(file)
                onPanelAsked: (panel, lesson) => { root.openFile(panel); editor.showLesson(lesson) }
                onNewLessonAsked: root.newLessonAsked()
                onMaterialAsked: root.materialAsked()
                onSourcesAsked: lesson => root.sourcesAsked(lesson)
                onNewVersionAsked: used => root.newVersionAsked(used)
                onCompileAsked: file => { root.openFile(file); root.compileFile() }
                onRenameAsked: file => root.renameAsked(file)
            }
            }
            SplitView {
                SplitView.fillWidth: true
                orientation: Qt.Vertical
                handle: Handle {}

                Item {
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 120
                    SplitView {
                    id: sideBySide
                    anchors.fill: parent
                    orientation: root.stacked ? Qt.Vertical : Qt.Horizontal
                    handle: Handle {}
                    onWidthChanged: root.applySplit()
                    onHeightChanged: if (root.stacked) root.applySplit()
                    onResizingChanged: if (resizing) root.split = -1
                    EditorPane {
                        id: editor
                        SplitView.minimumWidth: 0
                        onJumpAsked: (file, line) => root.jumpToPdf(file, line)
                        onChatAsked: root.chatOpen = true
                        onManualAsked: (lesson, block, line, what) => root.startManual(lesson, block, line, what)
                        // «Impagina di nuovo»: prima si salvano le schede, perché la lezione si misura com'è
                        onPaginateAsked: lesson => { editor.saveAll(); Workspace.paginateLesson(lesson) }
                        // Rivedi figure: le figure scritte fuori entrano nel registro (il segno va nelle lezioni: prima si salva)
                        onAdoptFiguresAsked: { if (!root.manual && !Workspace.busy) { editor.saveAll(); Workspace.adoptFigures() } }
                        // chiudere la lezione dall'editor è lasciare la modifica a mano
                        onCurrentFileChanged: root.syncReview()
                        onTabClosed: file => { if (root.manual && file === root.manual.lesson) root.endManual(false) }
                    }
                    PdfPane {
                        id: pdf
                        SplitView.fillWidth: true
                        busy: Workspace.compiling
                        SplitView.minimumWidth: 0
                        SplitView.fillHeight: true
                        source: Workspace.pdf
                        onSourceAsked: (page, x, y) => root.jumpToSource(page, x, y)
                        tags: Workspace.showTags ? Workspace.tags : []
                        hiddenMarks: Workspace.tagSources.filter(s => s.hidden).map(s => s.mark)
                        onTagClicked: (path, sourcePage) => path !== "" ? root.openFile(path, sourcePage) : Workspace.say("La fonte di questo blocco non è più nel progetto.")
                    }
                    }
                    // la capsula sul divisore: PDF pieno, alla pari, editor pieno
                    Rectangle {
                        z: 10
                        x: Math.max(4, Math.min(editor.width - width / 2, sideBySide.width - width - 4))
                        y: sideBySide.height - height - Theme.gap
                        width: capsule.implicitWidth + 4
                        height: Theme.rowHeight - 4
                        radius: height / 2
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                        Row {
                            id: capsule
                            anchors.centerIn: parent
                            Repeater {
                                model: [{ glyph: "←", hint: "PDF a tutta larghezza" }, { glyph: "≡", hint: "Editor e PDF alla pari" }, { glyph: "→", hint: "Editor a tutta larghezza" }]
                                TxButton {
                                    required property var modelData
                                    required property int index
                                    text: modelData.glyph
                                    hint: modelData.hint
                                    flat: true
                                    implicitWidth: 24
                                    implicitHeight: Theme.rowHeight - 8
                                    radius: height / 2
                                    onClicked: { root.split = -1; root.split = index }
                                }
                            }
                        }
                    }
                }
                BottomPanel {
                    id: bottom
                    visible: root.bottomOpen
                    SplitView.preferredHeight: 170
                    SplitView.minimumHeight: 80
                    onIssueChosen: (file, line) => root.openFile(file, line)
                    onCloseAsked: root.bottomOpen = false
                    onFixAsked: { editor.saveAll(); Workspace.fixAll() }
                }
            }
            ChatPane {
                id: chat
                visible: root.chatOpen
                onShotAsked: what => root.shot(what)
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 280
                SplitView.maximumWidth: Math.max(280, root.width / 2)
                onCloseAsked: root.chatOpen = false
            }
        }
        StatusBar {
            Layout.fillWidth: true
            onClicked: root.bottomOpen = !root.bottomOpen
        }
    }
}
