import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Il profilo del progetto: come si scrive per questo corso. Le cinque sezioni impaginate, ognuna modificabile, e le
// regole in più; sotto, chiuso, ciò che Texfix fa sempre. Ciò che si cambia qui è del progetto: la libreria e gli altri
// progetti non ne sanno niente finché non lo si salva nella libreria.
Rectangle {
    id: root
    color: Theme.window
    signal closeAsked()
    signal chatAsked()

    property int revision: 0
    Connections { target: Workspace; function onSettingsChanged() { root.revision++ } }
    readonly property var sections: revision >= 0 ? Workspace.ruleSections() : []
    readonly property var structural: Workspace.structuralSections()
    property string editing                            // l'id della sezione che si modifica
    property bool showStructural: false
    property string note

    TxScroll {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 4 * Theme.gap
        ColumnLayout {
            id: column
            x: 3 * Theme.gap
            y: 2 * Theme.gap
            width: Math.min(parent.width - 6 * Theme.gap, 900)
            spacing: Theme.gap

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    TxLabel { Layout.fillWidth: true; text: "Profilo"; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
                    TxLabel { Layout.fillWidth: true; text: root.revision >= 0 ? Workspace.profileName : ""; font.pixelSize: Theme.textSize + 6; font.weight: Font.DemiBold }
                }
                TxButton { text: "Migliora con l'agente"; onClicked: { Workspace.askAgent("migliora-profilo"); root.chatAsked() } }
                TxButton { text: "Salva nella libreria…"; onClicked: { saveName.text = Workspace.profileName; saveSheet.error = ""; saveSheet.open() } }
                TxButton { text: "Esporta…"; onClicked: exportDialog.open() }
                TxButton { text: "Chiudi"; flat: true; onClicked: root.closeAsked() }
            }
            TxLabel {
                Layout.fillWidth: true
                text: "Come si scrive per questo corso. Si cambia qui, sezione per sezione: vale per questo progetto. Per usarlo anche in altri, salvalo nella libreria."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            TxLabel { Layout.fillWidth: true; visible: root.note !== ""; text: root.note; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }

            Repeater {
                model: root.sections
                ColumnLayout {
                    id: section
                    required property var modelData
                    readonly property bool open: root.editing === modelData.id
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.gap
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        TxLabel { Layout.fillWidth: true; text: section.modelData.title; font.pixelSize: Theme.textSize + 2; font.weight: Font.DemiBold }
                        TxButton {
                            visible: section.modelData.custom && !section.open
                            text: "Rimetti com'era"
                            flat: true
                            hint: "La sezione come nel profilo da cui viene"
                            onClicked: Workspace.resetRule(section.modelData.id)
                        }
                        TxButton {
                            text: section.open ? "Fatto" : "Modifica"
                            primary: section.open
                            onClicked: {
                                if (section.open) {
                                    Workspace.setRuleText(section.modelData.id, editor.text)
                                    root.editing = ""
                                } else {
                                    editor.text = section.modelData.text
                                    root.editing = section.modelData.id
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !section.open
                        text: section.modelData.text !== "" ? section.modelData.text : "_(vuota)_"
                        textFormat: Text.MarkdownText
                        wrapMode: Text.WordWrap
                        color: section.modelData.text !== "" ? Theme.text : Theme.textFaint
                        font.pixelSize: Theme.textSize
                        lineHeight: 1.15
                    }
                    TxTextArea {
                        id: editor
                        Layout.fillWidth: true
                        visible: section.open
                        mono: true
                    }
                }
            }

            // le regole in più: una per riga, valgono più di tutte le altre
            TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: "Regole in più"; font.pixelSize: Theme.textSize + 2; font.weight: Font.DemiBold }
            TxLabel {
                Layout.fillWidth: true
                text: "Una per riga. Valgono più di tutte le altre; «Ricorda come regola» in Rivedi le aggiunge qui."
                color: Theme.textMuted
                font.pixelSize: Theme.smallSize
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            TxTextArea {
                id: extra
                Layout.fillWidth: true
                minHeight: 2 * Theme.rowHeight
                placeholder: "Per esempio: Kant si cita dall'edizione dell'Accademia."
                Component.onCompleted: text = Workspace.projectRules.join("\n")
                onEdited: Workspace.projectRules = text.split("\n")
            }

            // ciò che Texfix fa sempre: uguale per ogni profilo
            TxLabel {
                Layout.fillWidth: true
                Layout.topMargin: 2 * Theme.gap
                text: (root.showStructural ? "▾ " : "▸ ") + "Ciò che Texfix fa sempre"
                font.weight: Font.Medium
                color: Theme.textMuted
                MouseArea { anchors.fill: parent; onClicked: root.showStructural = !root.showStructural }
            }
            Repeater {
                model: root.showStructural ? root.structural : []
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    TxLabel { Layout.fillWidth: true; text: parent.modelData.title; font.weight: Font.Medium; color: Theme.textMuted }
                    Text {
                        Layout.fillWidth: true
                        text: parent.modelData.text
                        textFormat: Text.MarkdownText
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.smallSize + 1
                    }
                }
            }
        }
    }

    TxSheet {
        id: saveSheet
        title: "Salva il profilo nella libreria"
        property string error
        TxLabel {
            Layout.fillWidth: true
            text: "Con un nome suo, diverso da quelli che ci sono: i profili di Texfix e i tuoi."
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        TxTextArea { id: saveName; Layout.fillWidth: true; minHeight: Theme.rowHeight; placeholder: "Il nome del profilo" }
        TxLabel { Layout.fillWidth: true; visible: saveSheet.error !== ""; text: saveSheet.error; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        buttons: [
            TxButton { text: "Annulla"; onClicked: saveSheet.close() },
            TxButton {
                visible: saveSheet.error.startsWith("C'è già") && Workspace.libraryProfiles().includes(saveName.text.trim())
                text: "Sostituiscilo"
                onClicked: { saveSheet.error = Workspace.saveProfile(saveName.text.trim(), true); if (saveSheet.error === "") saveSheet.close() }
            },
            TxButton {
                text: "Salva"
                primary: true
                onClicked: { saveSheet.error = Workspace.saveProfile(saveName.text.trim()); if (saveSheet.error === "") saveSheet.close() }
            }
        ]
    }
    FileDialog {
        id: exportDialog
        title: "Esporta il profilo"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Profili di Texfix (*.profilo.md)"]
        onAccepted: root.note = Workspace.exportProfile(Workspace.localPath(selectedFile))
    }
}
