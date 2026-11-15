import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQml.Models
import Texfix

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    minimumWidth: 480
    minimumHeight: 380
    visible: true
    title: Workspace.hasProject ? Workspace.name + " — Texfix" : "Texfix"
    color: Theme.window

    // Importa un .sty nel progetto. Uno stile già descritto si usa subito; degli altri si chiede a che cosa serve
    // ogni ambiente, poi il progetto passa a quello stile.
    function importStyle(path) {
        const result = Workspace.importStyle(path)
        if (result.error !== "") {
            notice.text = result.error
            return notice.open()
        }
        settingsSheet.close()
        if (result.described)
            Workspace.applyStyle(result.name)
        else
            rolesSheet.ask(result.name, result.environments)
    }

    function askConvert() {
        if (Workspace.jobRunning)
            return Workspace.say("C'è già un lavoro in corso.")
        convertSheet.open()
    }

    function openFolder(path) {
        const answer = Workspace.openFolder(path)
        if (answer === "prepara") {
            prepare.path = path
            prepare.open()
        } else if (answer !== "") {
            notice.text = answer
            notice.open()
        }
    }

    // I menu non ripetono i tasti della finestra: ciò che ha un tasto ha solo la sua scorciatoia (più sotto).
    menuBar: MenuBar {
        Menu {
            title: "File"
            Action { text: "Nuovo progetto…"; shortcut: StandardKey.New; onTriggered: newDialog.open() }
            Action { text: "Apri cartella…"; shortcut: StandardKey.Open; onTriggered: openDialog.open() }
            MenuSeparator {}
            Action { text: "Salva"; shortcut: StandardKey.Save; enabled: Workspace.hasProject; onTriggered: project.save() }
            Action { text: "Chiudi progetto"; enabled: Workspace.hasProject; onTriggered: Workspace.closeProject() }
            MenuSeparator {}
            Action { text: "Preferenze…"; onTriggered: preferencesSheet.open() }
        }
        Menu {
            id: editMenu
            title: "Modifica"
            property string undoTitle
            property string redoTitle
            property var steps: []
            onAboutToShow: {
                undoTitle = Workspace.undoTitle()
                redoTitle = Workspace.redoTitle()
                steps = Workspace.historySteps()
            }
            Action {
                text: editMenu.undoTitle !== "" ? "Annulla «" + editMenu.undoTitle + "»" : "Annulla"
                shortcut: StandardKey.Undo
                enabled: Workspace.hasProject
                onTriggered: project.undo()
            }
            Action {
                text: editMenu.redoTitle !== "" ? "Ripeti «" + editMenu.redoTitle + "»" : "Ripeti"
                shortcut: StandardKey.Redo
                enabled: Workspace.hasProject
                onTriggered: project.redo()
            }
            MenuSeparator {}
            Menu {
                id: stepsMenu
                title: "Cronologia del progetto"
                enabled: Workspace.hasProject
                Instantiator {
                    model: editMenu.steps
                    delegate: MenuItem {
                        required property var modelData
                        required property int index
                        text: (modelData.current ? "● " : "    ") + modelData.title + "  —  " + modelData.when
                        onTriggered: project.goToStep(index)
                    }
                    onObjectAdded: (index, object) => stepsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => stepsMenu.removeItem(object)
                }
            }
        }
        Menu {
            title: "Progetto"
            Action { text: "Riorganizza per argomenti…"; enabled: Workspace.hasProject && !Workspace.byTopic; onTriggered: { project.save(); Workspace.proposeTopics() } }
            Action { text: "Riorganizza per data di lezione"; enabled: Workspace.hasProject && Workspace.byTopic; onTriggered: { project.save(); Workspace.backToLessons() } }
            MenuSeparator {}
            Action { text: "Riepilogo dubbi"; enabled: Workspace.hasProject; onTriggered: project.openFile("rivedi:dubbi") }
            Action { text: "Riepilogo correzioni"; enabled: Workspace.hasProject; onTriggered: project.openFile("rivedi:correzioni") }
            Action { text: "Riepilogo figure"; enabled: Workspace.hasProject; onTriggered: project.openFile("rivedi:figure") }
            Action { text: "Riepilogo impaginazioni"; enabled: Workspace.hasProject; onTriggered: project.reviewLayout() }
            MenuSeparator {}
            Action { text: "Impostazioni del progetto…"; shortcut: "Ctrl+,"; enabled: Workspace.hasProject; onTriggered: settingsSheet.open() }
        }
        Menu {
            title: "Vista"
            Action { text: "A capo nell'editor"; shortcut: "Alt+Z"; enabled: Workspace.hasProject; onTriggered: project.toggleWrap() }
        }
    }

    // Le scorciatoie arrivano sempre all'azione, che dice perché non può.
    Shortcut { sequence: "Ctrl+B"; onActivated: Workspace.hasProject ? project.compileFile() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+Shift+B"; onActivated: Workspace.hasProject ? project.compileDocument() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+Shift+K"; onActivated: Workspace.hasProject ? window.askConvert() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: Workspace.openDoubts > 0 ? project.openFile("rivedi:dubbi") : Workspace.say("Non ci sono dubbi da decidere.") }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: Workspace.openDeclared > 0 ? project.openFile("rivedi:correzioni") : Workspace.say("Non ci sono aggiunte o correzioni da decidere.") }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: Workspace.hasProject ? project.openFile("rivedi:figure") : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+Shift+R"; onActivated: Workspace.hasProject ? project.reviewLayout() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+J"; onActivated: Workspace.hasProject ? project.jumpToCursor() : Workspace.say("Apri prima un progetto.") }

    Welcome {
        anchors.fill: parent
        visible: !Workspace.hasProject
        onNewAsked: newDialog.open()
        onOpenAsked: openDialog.open()
        onProjectChosen: path => window.openFolder(path)
    }
    ProjectView {
        id: project
        anchors.fill: parent
        onConvertAsked: window.askConvert()
        onNewLessonAsked: newLessonSheet.open()
        onMaterialAsked: materialDialog.open()
        onRenameAsked: file => renameSheet.ask(file)
        onSourcesAsked: lesson => { sourcesDialog.lesson = lesson; sourcesDialog.open() }
        visible: Workspace.hasProject
    }

    Connections {
        target: Workspace
        function onPrepareAsked(path) { prepare.path = path; prepare.open() }
    }

    FolderDialog {
        id: openDialog
        title: "Apri cartella"
        onAccepted: window.openFolder(Workspace.localPath(selectedFolder))
    }
    FolderDialog {
        id: newDialog
        title: "Nuovo progetto: scegli o crea una cartella vuota"
        acceptLabel: "Crea qui"
        onAccepted: {
            const error = Workspace.newProject(Workspace.localPath(selectedFolder))
            if (error !== "") {
                notice.text = error + " Per un progetto nuovo serve una cartella vuota; una cartella con dei file si apre con «Apri cartella»."
                notice.open()
            }
        }
    }

    TxSheet {
        id: prepare
        property string path
        TxLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; elide: Text.ElideNone; text: "Questa cartella non è ancora un progetto di Texfix. La preparo? Ciò che contiene va in «docs», fra i documenti da consultare; i file nascosti restano dove sono." }
        buttons: [
            TxButton { text: "Annulla"; onClicked: prepare.close() },
            TxButton { text: "Prepara"; primary: true; onClicked: { prepare.close(); Workspace.prepareFolder(prepare.path) } }
        ]
    }
    TxSheet {
        id: notice
        property string text
        TxLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; elide: Text.ElideNone; text: notice.text }
        buttons: TxButton { text: "Va bene"; primary: true; onClicked: notice.close() }
    }
    ConvertSheet {
        id: convertSheet
        onSettingsAsked: settingsSheet.open()
        onStarted: sources => { project.saveAll(); Workspace.convert(sources) }
    }
    OutcomeSheet { id: outcomeSheet }
    NewLessonSheet { id: newLessonSheet }
    RenameSheet { id: renameSheet }
    OutlineSheet { id: outlineSheet }
    Connections { target: Workspace; function onOutlineReady(topics) { outlineSheet.show(topics) } }
    FileDialog {
        id: materialDialog
        title: "Aggiungi materiale"
        fileMode: FileDialog.OpenFiles
        onAccepted: Workspace.sortMaterial(Workspace.addSources(Workspace.localPaths(selectedFiles)))
    }
    FileDialog {
        id: sourcesDialog
        property string lesson
        title: "Le fonti da aggiungere alla lezione"
        fileMode: FileDialog.OpenFiles
        onAccepted: Workspace.integrateSources(lesson, Workspace.localPaths(selectedFiles))
    }
    Connections { target: Workspace; function onOutcomeReady(outcome) { outcomeSheet.show(outcome) } }
    SettingsSheet { id: settingsSheet; onImportAsked: styleDialog.open() }
    StyleRolesSheet { id: rolesSheet; onConfirmed: Workspace.applyStyle(styleName) }
    FileDialog {
        id: styleDialog
        title: "Importa uno stile"
        nameFilters: ["Stili LaTeX (*.sty)"]
        onAccepted: window.importStyle(Workspace.localPath(selectedFile))
    }
    PreferencesSheet { id: preferencesSheet }
    Connections {
        target: Workspace
        function onGesture(name, argument) {
            if (name === "argomenti") {
                Workspace.proposeTopics()
            } else if (name === "applica-argomenti") {
                Workspace.say(Workspace.applyTopics(outlineSheet.topics))
                outlineSheet.close()
            } else if (name === "per-data") {
                Workspace.backToLessons()
            } else if (name === "converti") {
                if (argument === "finestra")
                    convertSheet.open()
                else
                    Workspace.convert(Workspace.convertSources().filter(s => s.convertible).map(s => s.file))
            } else if (name === "impostazioni")
                settingsSheet.open()
            else if (name === "stile")
                window.importStyle(argument)
            else if (name === "usa-stile")
                Workspace.applyStyle(argument)
            else if (name === "preferenze") {
                preferencesSheet.open()
                if (argument !== "")
                    preferencesSheet.current = parseInt(argument)
            }
        }
    }
}
