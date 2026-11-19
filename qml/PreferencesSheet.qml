import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Texfix

// Preferenze: le librerie, per tutti i progetti. I profili (come si scrive per un corso) e gli stili (come appare):
// quelli di Texfix e quelli dell'utente, che si importano e si tolgono. Un progetto ne usa uno per tipo, e lo cambia
// dalle sue Impostazioni; il profilo di un progetto si legge e si cambia nella sua finestra.
TxSheet {
    id: root
    title: "Preferenze: le mie librerie"
    wantedWidth: 760
    wantedHeight: 620

    property var sections: []
    property int current: 0
    property bool editing: false
    readonly property var section: sections.length > current ? sections[current] : null
    // dopo le regole, la libreria degli stili
    property var styles: []
    property string styleNote
    readonly property bool stylesPage: section !== null && section.id === "@stili"
    property var builtinProfiles: []
    property var myProfiles: []
    function reload() {
        sections = [{ id: "@profili", title: "Profili", text: "", custom: false }, { id: "@stili", title: "Stili", text: "", custom: false }]
        styles = Workspace.libraryStyles()
        builtinProfiles = Workspace.builtinProfiles()
        myProfiles = Workspace.libraryProfiles()
    }
    onAboutToShow: { editing = false; reload() }

    RowLayout {
        Layout.fillWidth: true
        spacing: 2 * Theme.gap

        // le sezioni
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 210
            Layout.minimumWidth: 160
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
            }
            // la libreria dei profili: quelli di Texfix (si usano così o si copiano cambiandoli in un progetto) e i tuoi
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.section !== null && root.section.id === "@profili"
                spacing: Theme.gap
                TxLabel {
                    Layout.fillWidth: true
                    text: "Come si scrive per un corso. Un progetto ne usa uno, che si sceglie nelle sue Impostazioni e si cambia nella sua finestra; per riusarne uno cambiato, lo si salva qui con un nome suo."
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
                Repeater {
                    model: root.builtinProfiles.concat(root.myProfiles)
                    RowLayout {
                        id: profileRow
                        required property string modelData
                        readonly property bool mine: root.myProfiles.includes(modelData)
                        property bool sure: false
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        TxLabel { text: profileRow.modelData }
                        TxLabel { Layout.fillWidth: true; text: profileRow.mine ? "tuo" : "di Texfix"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                        TxButton {
                            visible: profileRow.mine
                            text: profileRow.sure ? "Sì, togli" : "Togli"
                            flat: !profileRow.sure
                            onClicked: {
                                if (!profileRow.sure)
                                    return profileRow.sure = true
                                Workspace.removeProfile(profileRow.modelData)
                                root.reload()
                            }
                        }
                    }
                }
                TxButton { text: "Importa un profilo…"; onClicked: profileDialog.open() }
                TxLabel { Layout.fillWidth: true; visible: root.profileNote !== ""; text: root.profileNote; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }
            }
            // la libreria degli stili: quelli di Texfix e quelli messi qui; un progetto li prende dalle sue Impostazioni
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.stylesPage
                spacing: Theme.gap
                TxLabel {
                    Layout.fillWidth: true
                    text: "Gli stili pronti per i progetti. Un progetto ne usa uno alla volta: lo si sceglie nelle sue Impostazioni, e lo si cambia nell'editor."
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
        }
    }

    property string profileNote
    FileDialog {
        id: profileDialog
        title: "Un profilo di Texfix (.profilo.md)"
        nameFilters: ["Profili di Texfix (*.profilo.md *.md)"]
        onAccepted: { root.profileNote = Workspace.importProfileToLibrary(Workspace.localPath(selectedFile)); root.reload() }
    }
    FileDialog {
        id: libraryDialog
        title: "Uno stile (.sty) da mettere nella libreria"
        nameFilters: ["Stili LaTeX (*.sty)"]
        onAccepted: { root.styleNote = Workspace.addStyleToLibrary(Workspace.localPaths([selectedFile])[0]); root.reload() }
    }

    buttons: TxButton { text: "Chiudi"; primary: true; onClicked: root.close() }
}
