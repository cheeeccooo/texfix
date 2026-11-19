import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Impostazioni del progetto (⌘,): le scelte che valgono per ogni lavoro, il profilo e lo stile. Ogni scelta è
// salvata subito.
TxSheet {
    id: root
    title: "Impostazioni del progetto"
    wantedWidth: 560
    signal importAsked()
    signal programAsked()                              // si passa per argomento: prima la domanda sul programma

    readonly property var entries: [
        { key: "scrittura", label: "Come si scrive", hint: "Da studiare: prosa che spiega, correzioni dichiarate, aggiunte marcate. Fedele: uno a uno, niente di aggiunto." },
        { key: "qualita", label: "Qualità", hint: "Più qualità costa di più e ci mette di più." },
        { key: "figure_scansioni", label: "Figure delle scansioni", hint: "" },
        { key: "figure_tex", label: "Figure dei .tex", hint: "" },
        { key: "impaginazione", label: "Impaginazione", hint: "Da sola: dopo ogni conversione." }
    ]
    property int revision: 0                           // cambia quando cambiano le impostazioni: le voci si rileggono
    Connections { target: Workspace; function onSettingsChanged() { root.revision++ } }
    onAboutToShow: note = ""
    property string note

    // per data o per argomento: con delle lezioni il cambio passa da Riorganizza (la scaletta, o il ritorno per data)
    TxLabel { Layout.fillWidth: true; text: "Organizzazione"; font.weight: Font.Medium }
    TxSegmented {
        Layout.fillWidth: true
        choices: [{ value: "data", label: "Per data" }, { value: "argomento", label: "Per argomento" }]
        value: root.revision >= 0 && Workspace.byTopic ? "argomento" : "data"
        onChosen: value => {
            if ((value === "argomento") === Workspace.byTopic)
                return
            if (value === "argomento" && Workspace.program === "") {   // prima il programma del corso, se c'è
                root.close()
                return root.programAsked()
            }
            if (Workspace.lessons.length > 0)
                root.close()                           // si apre la scaletta, o si torna per data
            Workspace.setByTopic(value === "argomento")
        }
    }
    RowLayout {                                        // il programma del corso: l'ordine che gli argomenti seguono
        visible: root.revision >= 0 && Workspace.byTopic
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel {
            Layout.fillWidth: true
            text: Workspace.program === "" ? "Nessun programma del corso: l'ordine lo decide la logica del materiale."
                                           : "Programma del corso: " + Workspace.program.split("/").pop()
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        TxButton { text: Workspace.program === "" ? "Scegli…" : "Cambia…"; onClicked: programDialog.open() }
        TxButton { visible: Workspace.program !== ""; text: "Togli"; flat: true; onClicked: Workspace.setProgram("") }
    }
    FileDialog {
        id: programDialog
        title: "Il programma, l'indice o la scaletta del corso"
        onAccepted: Workspace.setProgram(Workspace.localPath(selectedFile))
    }
    TxLabel {
        Layout.fillWidth: true
        text: Workspace.byTopic ? Theme.keys("Il materiale nuovo va negli argomenti. Tornare per data rimette un file per lezione; si annulla con ⌘Z.")
                                : "Il materiale nuovo diventa lezioni in ordine di calendario. Passare per argomento propone una scaletta da sistemare."
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Item { implicitHeight: 4 }

    Repeater {
        model: root.entries
        ColumnLayout {
            id: entry
            required property var modelData
            Layout.fillWidth: true
            spacing: 4
            TxLabel { Layout.fillWidth: true; text: entry.modelData.label; font.weight: Font.Medium }
            TxSegmented {
                Layout.fillWidth: true
                choices: Workspace.settingChoices(entry.modelData.key)
                value: root.revision >= 0 ? Workspace.setting(entry.modelData.key) : ""
                onChosen: value => Workspace.setSetting(entry.modelData.key, value)
            }
            TxLabel {
                Layout.fillWidth: true
                visible: entry.modelData.hint !== ""
                text: entry.modelData.hint
                color: Theme.textMuted
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Item { implicitHeight: 4 }
        }
    }
    // il profilo e lo stile del progetto: quale, cambiarlo, importarne uno, esportarlo (in un file o nella libreria).
    // Il profilo si legge e si cambia nella sua finestra; lo stile nell'editor.
    TxLabel { Layout.fillWidth: true; text: "Profilo"; font.weight: Font.Medium }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel { Layout.fillWidth: true; text: root.revision >= 0 ? "«" + Workspace.profileName + "»: come si scrive per questo corso." : ""; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        TxButton { text: "Cambia…"; onClicked: profileMenu.popup() }
        TxButton { text: "Importa…"; onClicked: profileImport.open() }
        TxButton { text: "Esporta…"; onClicked: profileExport.popup() }
    }
    Menu {
        id: profileMenu
        Instantiator {
            model: root.revision >= 0 ? Workspace.profiles() : []
            delegate: MenuItem {
                required property string modelData
                text: modelData + (modelData === Workspace.profileName ? "  ✓" : "")
                onTriggered: root.note = Workspace.useProfile(modelData)
            }
            onObjectAdded: (index, object) => profileMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => profileMenu.removeItem(object)
        }
    }
    Menu {
        id: profileExport
        MenuItem { text: "In un file…"; onTriggered: profileFile.open() }
        MenuItem { text: "Nella libreria…"; onTriggered: { saveName.text = Workspace.profileName; saveSheet.kind = "profilo"; saveSheet.error = ""; saveSheet.open() } }
    }
    FileDialog {
        id: profileImport
        title: "Un profilo di Texfix (.profilo.md)"
        nameFilters: ["Profili di Texfix (*.profilo.md *.md)"]
        onAccepted: root.note = Workspace.importProfile(Workspace.localPath(selectedFile))
    }
    FileDialog {
        id: profileFile
        title: "Esporta il profilo"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Profili di Texfix (*.profilo.md)"]
        onAccepted: root.note = Workspace.exportProfile(Workspace.localPath(selectedFile))
    }

    TxLabel { Layout.fillWidth: true; Layout.topMargin: 4; text: "Stile"; font.weight: Font.Medium }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel {
            Layout.fillWidth: true
            text: root.revision >= 0 ? "«" + (Workspace.styleName === "" ? "science_default" : Workspace.styleName) + "»" + (Workspace.styleModified ? ", cambiato in questo progetto." : ".") : ""
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        TxButton { text: "Cambia…"; onClicked: styleMenu.popup() }
        TxButton { text: "Importa…"; onClicked: root.importAsked() }
        TxButton { text: "Esporta…"; onClicked: styleExport.popup() }
    }
    Menu {
        id: styleMenu
        Instantiator {
            model: root.revision >= 0 ? Workspace.styleLibrary() : []
            delegate: MenuItem {
                required property string modelData
                text: modelData + (modelData === Workspace.styleName ? "  ✓" : "")
                onTriggered: if (modelData !== Workspace.styleName) { root.close(); Workspace.useLibraryStyle(modelData) }
            }
            onObjectAdded: (index, object) => styleMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => styleMenu.removeItem(object)
        }
    }
    Menu {
        id: styleExport
        MenuItem { text: "In una cartella…"; onTriggered: styleFolder.open() }
        MenuItem { text: "Nella libreria…"; onTriggered: { saveName.text = Workspace.styleName; saveSheet.kind = "stile"; saveSheet.error = ""; saveSheet.open() } }
    }
    FolderDialog {
        id: styleFolder
        title: "Dove esportare lo stile"
        onAccepted: root.note = Workspace.exportStyle(Workspace.localPath(selectedFolder))
    }
    TxLabel {
        Layout.fillWidth: true
        text: root.note !== "" ? root.note : Theme.keys("Cambiare stile riscrive tutte le lezioni per il nuovo stile. Si annulla con ⌘Z.")
        color: root.note !== "" ? Theme.warn : Theme.textMuted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    TxSheet {
        id: saveSheet
        property string kind
        property string error
        title: kind === "stile" ? "Salva lo stile nella libreria" : "Salva il profilo nella libreria"
        TxLabel {
            Layout.fillWidth: true
            text: saveSheet.kind === "stile" ? "Con un nome suo, una parola sola (lettere, cifre, - e _). Se è un altro nome, lo stile cambia nome anche in questo progetto."
                                             : "Con un nome suo, diverso da quelli che ci sono: i profili di Texfix e i tuoi."
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        TxTextArea { id: saveName; Layout.fillWidth: true; minHeight: Theme.rowHeight; placeholder: "Il nome" }
        TxLabel { Layout.fillWidth: true; visible: saveSheet.error !== ""; text: saveSheet.error; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        buttons: [
            TxButton { text: "Annulla"; onClicked: saveSheet.close() },
            TxButton {
                visible: saveSheet.error.startsWith("C'è già")
                text: "Sostituiscilo"
                onClicked: {
                    saveSheet.error = saveSheet.kind === "stile" ? Workspace.saveStyleToLibrary(saveName.text.trim(), true) : Workspace.saveProfile(saveName.text.trim(), true)
                    if (saveSheet.error === "") saveSheet.close()
                }
            },
            TxButton {
                text: "Salva"
                primary: true
                onClicked: {
                    saveSheet.error = saveSheet.kind === "stile" ? Workspace.saveStyleToLibrary(saveName.text.trim()) : Workspace.saveProfile(saveName.text.trim())
                    if (saveSheet.error === "") saveSheet.close()
                }
            }
        ]
    }

    buttons: TxButton { text: "Chiudi"; primary: true; onClicked: root.close() }
}
