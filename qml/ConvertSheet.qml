import QtQuick
import QtQuick.Layouts
import Texfix

// Converti: le fonti di «Da convertire», ognuna una lezione nuova; in basso le scelte del progetto e il costo stimato.
TxSheet {
    id: root
    title: "Converti"
    wantedWidth: 560
    signal settingsAsked()
    signal started(var sources)

    property var sources: []
    property real estimate: 0
    property int revision: 0
    readonly property var convertible: sources.filter(s => s.convertible).map(s => s.file)
    function reload() {
        sources = Workspace.convertSources()
        estimate = Workspace.convertEstimate()
        revision++
    }
    onAboutToShow: reload()
    Connections { target: Workspace; function onSettingsChanged() { if (root.opened) root.reload() } }

    function choice(key) {
        const value = Workspace.setting(key)
        for (const c of Workspace.settingChoices(key))
            if (c.value === value)
                return c.label
        return ""
    }

    TxLabel {
        Layout.fillWidth: true
        visible: root.sources.length === 0
        text: "Non c'è niente da convertire: trascina le fonti in «Da convertire» (la cartella fonti/coda del progetto)."
        color: Theme.textMuted
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Repeater {
        model: root.sources
        RowLayout {
            id: row
            required property var modelData
            Layout.fillWidth: true
            spacing: Theme.gap
            opacity: modelData.convertible ? 1 : 0.5
            TxLabel { Layout.fillWidth: true; text: row.modelData.name; elide: Text.ElideMiddle }
            TxLabel { text: row.modelData.detail; color: Theme.textMuted; font.pixelSize: Theme.smallSize }
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; visible: root.sources.length > 0 }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel {
            Layout.fillWidth: true
            text: root.revision >= 0 ? [root.choice("scrittura"), "qualità " + root.choice("qualita").toLowerCase(),
                                        "figure: " + root.choice("figure_scansioni").toLowerCase(),
                                        "impaginazione " + root.choice("impaginazione").toLowerCase()].join(" · ") : ""
            color: Theme.textMuted
            font.pixelSize: Theme.smallSize
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        TxButton { text: "Cambia"; onClicked: root.settingsAsked() }
    }
    TxLabel {
        Layout.fillWidth: true
        visible: root.convertible.length > 0
        text: "Costo stimato: circa " + root.estimate.toLocaleString(Qt.locale("it_IT"), "f", 2) + " $"
        font.weight: Font.Medium
    }

    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton {
            text: "Converti"
            primary: true
            enabled: root.convertible.length > 0
            onClicked: { root.close(); root.started(root.convertible) }
        }
    ]
}
