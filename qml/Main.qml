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
            Workspace.proposeStyle(result.name)        // il modello legge lo stile e propone i ruoli: poi le domande
    }

    function createProject(path, profile, byTopic, program) {
        const error = Workspace.newProject(path, profile, byTopic === true)
        if (error === "" && program)
            Workspace.setProgram(program)
        if (error !== "") {
            notice.text = error + " Per un progetto nuovo serve una cartella vuota; una cartella con dei file si apre con «Apri cartella»."
            notice.open()
        }
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
            MenuSeparator {}
            Action { text: "Tag di provenienza"; checkable: true; checked: Workspace.showTags; enabled: Workspace.hasProject; onTriggered: Workspace.showTags = !Workspace.showTags }
            Menu {
                id: marksMenu
                title: "Fonti (legenda)"
                enabled: Workspace.hasProject && Workspace.showTags
                Instantiator {
                    model: Workspace.showTags ? Workspace.tagSources : []
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.mark + "  " + modelData.name
                        checkable: true
                        checked: !modelData.hidden
                        onTriggered: Workspace.setMarkHidden(modelData.mark, !modelData.hidden)
                    }
                    onObjectAdded: (index, object) => marksMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => marksMenu.removeItem(object)
                }
            }
        }
    }

    // Le scorciatoie arrivano sempre all'azione, che dice perché non può.
    Shortcut { sequence: "Ctrl+B"; onActivated: Workspace.hasProject ? project.compileFile() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+Shift+B"; onActivated: Workspace.hasProject ? project.compileDocument() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+Shift+K"; onActivated: Workspace.hasProject ? materialDialog.open() : Workspace.say("Apri prima un progetto.") }
    Shortcut { sequence: "Ctrl+L"; onActivated: Workspace.hasProject ? project.quoteSelection() : Workspace.say("Apri prima un progetto.") }
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
        onMaterialAsked: materialDialog.open()
        onRenameAsked: file => renameSheet.ask(file)
        onSourcesAsked: lesson => { sourcesDialog.lesson = lesson; sourcesDialog.open() }
        onNewVersionAsked: used => { versionDialog.used = used; versionDialog.open() }
        visible: Workspace.hasProject
    }

    Connections {
        target: Workspace
        function onPrepareAsked(path) { prepare.path = path; prepare.open() }
        function onSettingsAsked() { settingsSheet.open() }
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
            profileSheet.ask(Workspace.localPath(selectedFolder))   // per data o per argomento, e da quale profilo
        }
    }

    FileDialog {
        id: versionDialog
        property string used
        title: "La versione nuova della fonte"
        fileMode: FileDialog.OpenFile
        onAccepted: Workspace.newVersion(used, Workspace.localPaths([selectedFile])[0])
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
    OutcomeSheet { id: outcomeSheet; onLogAsked: project.showConsole() }
    RenameSheet { id: renameSheet }
    ProfileSheet { id: profileSheet; onChosen: (path, profile, byTopic, program) => window.createProject(path, profile, byTopic, program) }
    OutlineSheet { id: outlineSheet }
    Connections { target: Workspace; function onOutlineReady(topics) { outlineSheet.show(topics) } }
    FileDialog {
        id: materialDialog
        title: "Aggiungi materiale"
        fileMode: FileDialog.OpenFiles
        onAccepted: Workspace.addMaterial(Workspace.localPaths(selectedFiles))
    }
    FileDialog {
        id: sourcesDialog
        property string lesson
        title: "Il materiale da aggiungere alla lezione"
        fileMode: FileDialog.OpenFiles
        onAccepted: Workspace.addMaterial(Workspace.localPaths(selectedFiles), lesson)
    }
    Connections { target: Workspace; function onOutcomeReady(outcome) { outcomeSheet.show(outcome) } }
    SettingsSheet { id: settingsSheet; onImportAsked: styleDialog.open(); onProgramAsked: programSheet.open() }
    // il primo passaggio per argomento: il programma del corso, se c'è, dà l'ordine degli argomenti
    TxSheet {
        id: programSheet
        title: "Il programma del corso"
        TxLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            text: "Hai il programma, l'indice o la scaletta del corso? Con quello Texfix dispone gli argomenti nell'ordine del corso; senza, li dispone nell'ordine più logico per il materiale. Lo puoi aggiungere quando vuoi dalle impostazioni."
        }
        buttons: [
            TxButton { text: "Non ce l'ho"; onClicked: { programSheet.close(); Workspace.setByTopic(true) } },
            TxButton { text: "Scegli il file…"; primary: true; onClicked: { programSheet.close(); programAsk.open() } }
        ]
    }
    FileDialog {
        id: programAsk
        title: "Il programma, l'indice o la scaletta del corso"
        onAccepted: { Workspace.setProgram(Workspace.localPath(selectedFile)); Workspace.setByTopic(true) }
        onRejected: Workspace.setByTopic(true)
    }
    StyleRolesSheet { id: rolesSheet; onConfirmed: Workspace.applyStyle(styleName) }
    StyleMatchSheet { id: matchSheet }
    Connections {
        target: Workspace
        function onStyleProposed(name, environments) { if (environments.length > 0) rolesSheet.ask(name, environments); else Workspace.applyStyle(name) }
        function onStyleQuestions(name, questions) { settingsSheet.close(); matchSheet.ask(name, questions) }
    }
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
            if (name === "nuovo" || name === "nuovo-argomenti") {
                window.createProject(argument.split(" ")[0], argument.split(" ").slice(1).join(" "), name === "nuovo-argomenti")
            } else if (name === "versione-nuova") {
                Workspace.newVersion(argument.split(" ")[0], argument.split(" ")[1])
            } else if (name === "programma") {
                Workspace.setProgram(argument)
            } else if (name === "salva-profilo") {
                Workspace.say(Workspace.saveProfile(argument))
            } else if (name === "argomenti") {
                Workspace.proposeTopics()
            } else if (name === "applica-argomenti") {
                Workspace.say(Workspace.applyTopics(outlineSheet.topics))
                outlineSheet.close()
            } else if (name === "per-data") {
                Workspace.backToLessons()
            } else if (name === "impostazioni")
                settingsSheet.open()
            else if (name === "stile")
                window.importStyle(argument)
            else if (name === "libreria")
                Workspace.useLibraryStyle(argument)
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
