import QtQuick
import QtQuick.Layouts
import Texfix

// Impostazioni del progetto (⌘,): le scelte che valgono per ogni lavoro e le regole del progetto. Ogni scelta è
// salvata subito.
TxSheet {
    id: root
    title: "Impostazioni del progetto"
    wantedWidth: 560
    signal importAsked()
    property string profileNote

    readonly property var entries: [
        { key: "scrittura", label: "Come si scrive", hint: "Da studiare: prosa che spiega, correzioni dichiarate, aggiunte marcate. Fedele: uno a uno, niente di aggiunto." },
        { key: "qualita", label: "Qualità", hint: "Più qualità costa di più e ci mette di più." },
        { key: "figure_scansioni", label: "Figure delle scansioni", hint: "" },
        { key: "figure_tex", label: "Figure dei .tex", hint: "" },
        { key: "impaginazione", label: "Impaginazione", hint: "Da sola: dopo ogni conversione." }
    ]
    property int revision: 0                           // cambia quando cambiano le impostazioni: le voci si rileggono
    Connections { target: Workspace; function onSettingsChanged() { root.revision++ } }
    onAboutToShow: rules.text = Workspace.projectRules.join("\n")

    // per data o per argomento: con delle lezioni il cambio passa da Riorganizza (la scaletta, o il ritorno per data)
    TxLabel { Layout.fillWidth: true; text: "Organizzazione"; font.weight: Font.Medium }
    TxSegmented {
        Layout.fillWidth: true
        choices: [{ value: "data", label: "Per data" }, { value: "argomento", label: "Per argomento" }]
        value: root.revision >= 0 && Workspace.byTopic ? "argomento" : "data"
        onChosen: value => {
            if ((value === "argomento") === Workspace.byTopic)
                return
            if (Workspace.lessons.length > 0)
                root.close()                           // si apre la scaletta, o si torna per data
            Workspace.setByTopic(value === "argomento")
        }
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
    TxLabel { Layout.fillWidth: true; text: "Stile"; font.weight: Font.Medium }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel {
            Layout.fillWidth: true
            text: Workspace.styleName === "" ? "Nessuno: un classico documento matematico." : "«" + Workspace.styleName + "»"
            color: Theme.textMuted
        }
        TxButton { visible: Workspace.styleName !== ""; text: "Torna a nessuno stile"; onClicked: Workspace.applyStyle("") }
        Repeater {                                     // gli stili della libreria, pronti
            model: Workspace.styleLibrary()
            TxButton {
                required property string modelData
                visible: Workspace.styleName !== modelData
                text: "Usa «" + modelData + "»"
                onClicked: { root.close(); Workspace.useLibraryStyle(modelData) }
            }
        }
        TxButton { text: "Importa uno stile…"; onClicked: root.importAsked() }
    }
    TxLabel {
        Layout.fillWidth: true
        text: Theme.keys("Cambiare stile riscrive tutte le lezioni per il nuovo stile. Si annulla con ⌘Z.")
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Item { implicitHeight: 4 }
    TxLabel { Layout.fillWidth: true; text: "Profilo di partenza"; font.weight: Font.Medium }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight + 4
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: profileName.activeFocus ? Theme.accent : Theme.border
            TextInput {
                id: profileName
                anchors.fill: parent
                anchors.leftMargin: Theme.gap
                anchors.rightMargin: Theme.gap
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.text
                font.pixelSize: Theme.textSize
                selectByMouse: true
                clip: true
                onTextEdited: root.profileNote = ""
                TxLabel { anchors.fill: parent; visible: parent.text === ""; text: "Il nome del profilo"; color: Theme.textFaint }
            }
        }
        TxButton {
            text: "Salva come profilo"
            enabled: profileName.text.trim() !== ""
            onClicked: {
                const error = Workspace.saveProfile(profileName.text)
                root.profileNote = error !== "" ? error : "Salvato. Un progetto nuovo può partire da «" + profileName.text.trim() + "»."
            }
        }
    }
    TxLabel {
        Layout.fillWidth: true
        text: root.profileNote !== "" ? root.profileNote : "Salva lo stile, queste impostazioni e le regole del progetto, per riusarli in un progetto nuovo."
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Item { implicitHeight: 4 }
    TxLabel { Layout.fillWidth: true; text: "Regole del progetto"; font.weight: Font.Medium }
    TxLabel {
        Layout.fillWidth: true
        text: "Una per riga. Valgono più delle regole delle Preferenze, solo in questo progetto."
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    TxTextArea {
        id: rules
        Layout.fillWidth: true
        placeholder: "Per esempio: i vettori si scrivono con la freccia."
        onEdited: Workspace.projectRules = text.split("\n")
    }

    buttons: TxButton { text: "Chiudi"; primary: true; onClicked: root.close() }
}
