import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Preferenze: le regole editoriali, per tutti i progetti. Una sezione alla volta: si legge impaginata, si modifica,
// si riporta al predefinito.
TxSheet {
    id: root
    title: "Preferenze: come voglio i miei appunti"
    wantedWidth: 760
    wantedHeight: 620

    property var sections: []
    property int current: 0
    property bool editing: false
    readonly property var section: sections.length > current ? sections[current] : null
    // dopo le regole, la libreria degli stili
    property var styles: []
    property string styleNote
    readonly property bool onStyles: section !== null && section.id === "@stili"
    function reload() {
        sections = Workspace.ruleSections().concat([{ id: "@stili", title: "Stili", text: "", custom: false }])
        styles = Workspace.libraryStyles()
    }
    onAboutToShow: { editing = false; reload() }

    RowLayout {
        Layout.fillWidth: true
        spacing: 2 * Theme.gap

        // le sezioni
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
                        Layout.maximumWidth: 230
            spacing: 2
            Repeater {
                model: root.sections
                Rectangle {
                    id: item
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: Theme.rowHeight + 2
                    radius: Theme.radius
                    color: index === root.current ? Theme.selected : area.containsMouse ? Theme.hover : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gap
                        anchors.rightMargin: Theme.gap
                        TxLabel { Layout.fillWidth: true; text: item.modelData.title }
                        TxLabel { visible: item.modelData.custom; text: "cambiata"; color: Theme.accent; font.pixelSize: Theme.smallSize }
                    }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: { root.editing = false; root.current = item.index } }
                }
            }
        }

        // la sezione scelta
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.gap
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                TxLabel { Layout.fillWidth: true; text: root.section ? root.section.title : ""; font.weight: Font.DemiBold; font.pixelSize: Theme.textSize + 1 }
                TxButton {
                    visible: root.section !== null && root.section.custom && !root.onStyles
                    text: "Riporta al predefinito"
                    onClicked: { Workspace.resetRule(root.section.id); root.editing = false; root.reload() }
                }
                TxButton {
                    visible: !root.onStyles
                    text: root.editing ? "Fatto" : "Modifica"
                    primary: root.editing
                    onClicked: {
                        if (root.editing) {
                            Workspace.setRuleText(root.section.id, editor.text)
                            root.reload()
                        } else {
                            editor.text = root.section.text
                        }
                        root.editing = !root.editing
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !root.editing && !root.onStyles
                text: root.section ? root.section.text : ""
                textFormat: Text.MarkdownText
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: Theme.textSize
                lineHeight: 1.15
            }
            // la libreria degli stili: quelli di Texfix e quelli messi qui; un progetto li prende dalle sue Impostazioni
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.onStyles
                spacing: Theme.gap
                TxLabel {
                    Layout.fillWidth: true
                    text: "Gli stili pronti per i progetti. Un progetto ne usa uno alla volta, o nessuno: lo si sceglie nelle Impostazioni del progetto."
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                Repeater {
                    model: root.styles
                    RowLayout {
                        id: entry
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        TxLabel { text: entry.modelData.name; font.family: Theme.monoFamily }
                        TxLabel { Layout.fillWidth: true; text: entry.modelData.bundled ? "di Texfix" : "tuo"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                        TxButton {
                            visible: !entry.modelData.bundled
                            text: "Togli"
                            flat: true
                            onClicked: { Workspace.removeLibraryStyle(entry.modelData.name); root.reload() }
                        }
                    }
                }
                TxButton { text: "Aggiungi uno stile…"; onClicked: libraryDialog.open() }
                TxLabel { Layout.fillWidth: true; visible: root.styleNote !== ""; text: root.styleNote; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }
            }
            TxTextArea {
                id: editor
                Layout.fillWidth: true
                visible: root.editing
                mono: true
            }
        }
    }

    FileDialog {
        id: libraryDialog
        title: "Uno stile (.sty) da mettere nella libreria"
        nameFilters: ["Stili LaTeX (*.sty)"]
        onAccepted: { root.styleNote = Workspace.addStyleToLibrary(Workspace.localPaths([selectedFile])[0]); root.reload() }
    }

    buttons: TxButton { text: "Chiudi"; primary: true; onClicked: { if (root.editing) Workspace.setRuleText(root.section.id, editor.text); root.close() } }
}
