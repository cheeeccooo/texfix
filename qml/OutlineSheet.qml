import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Texfix

// La scaletta di «Riorganizza per argomenti»: gli argomenti proposti, da sistemare. Si rinomina un argomento, lo si
// sposta, se ne crea uno nuovo; una sezione si sposta su e giù o in un altro argomento.
TxSheet {
    id: root
    title: "Riorganizza per argomenti"
    wantedWidth: 640
    wantedHeight: 680
    property var topics: []                            // [{title, sections: [{id, title, lesson}]}]
    property string error

    function show(proposal) { topics = proposal; error = ""; open() }
    function changed() { topics = topics.slice() }
    function moveTopic(t, by) {
        const to = t + by
        if (to < 0 || to >= topics.length)
            return
        const item = topics.splice(t, 1)[0]
        topics.splice(to, 0, item)
        changed()
    }
    function moveSection(t, s, by) {
        const list = topics[t].sections, to = s + by
        if (to < 0 || to >= list.length)
            return
        const item = list.splice(s, 1)[0]
        list.splice(to, 0, item)
        changed()
    }
    function sendSection(t, s, to) {
        if (to === topics.length)
            topics.push({ title: "Argomento nuovo", sections: [] })
        topics[to].sections.push(topics[t].sections.splice(s, 1)[0])
        topics = topics.filter(topic => topic.sections.length > 0)   // un argomento rimasto vuoto sparisce
    }

    TxLabel {
        Layout.fillWidth: true
        text: "Ogni argomento diventa un file. Il contenuto resta lo stesso, riga per riga; si torna indietro con «Riorganizza per data di lezione» o con ⌘Z."
        color: Theme.textMuted
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Repeater {
        model: root.topics
        ColumnLayout {
            id: topic
            required property var modelData
            required property int index
            Layout.fillWidth: true
            Layout.topMargin: Theme.gap
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.rowHeight + 4
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: 1
                    border.color: name.activeFocus ? Theme.accent : Theme.border
                    TextInput {
                        id: name
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gap
                        anchors.rightMargin: Theme.gap
                        verticalAlignment: TextInput.AlignVCenter
                        text: topic.modelData.title
                        color: Theme.text
                        font.pixelSize: Theme.textSize
                        font.weight: Font.DemiBold
                        selectByMouse: true
                        clip: true
                        onTextEdited: root.topics[topic.index].title = text
                    }
                }
                TxButton { text: "↑"; flat: true; implicitWidth: 26; enabled: topic.index > 0; hint: "Prima"; onClicked: root.moveTopic(topic.index, -1) }
                TxButton { text: "↓"; flat: true; implicitWidth: 26; enabled: topic.index < root.topics.length - 1; hint: "Dopo"; onClicked: root.moveTopic(topic.index, 1) }
            }
            Repeater {
                model: topic.modelData.sections
                RowLayout {
                    id: section
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap
                    spacing: 6
                    TxLabel { Layout.fillWidth: true; text: section.modelData.title }
                    TxLabel { text: "lezione " + section.modelData.lesson; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                    TxButton { text: "↑"; flat: true; implicitWidth: 24; enabled: section.index > 0; onClicked: root.moveSection(topic.index, section.index, -1) }
                    TxButton { text: "↓"; flat: true; implicitWidth: 24; enabled: section.index < topic.modelData.sections.length - 1; onClicked: root.moveSection(topic.index, section.index, 1) }
                    TxButton {
                        text: "Sposta ▾"
                        flat: true
                        onClicked: { moveMenu.fromTopic = topic.index; moveMenu.fromSection = section.index; moveMenu.popup() }
                    }
                }
            }
        }
    }
    Menu {
        id: moveMenu
        property int fromTopic
        property int fromSection
        Instantiator {
            model: root.topics.map(t => t.title).concat(["Argomento nuovo…"])
            delegate: MenuItem {
                required property string modelData
                required property int index
                text: modelData
                enabled: index !== moveMenu.fromTopic
                onTriggered: root.sendSection(moveMenu.fromTopic, moveMenu.fromSection, index)
            }
            onObjectAdded: (index, object) => moveMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => moveMenu.removeItem(object)
        }
    }
    TxLabel { Layout.fillWidth: true; visible: root.error !== ""; text: root.error; color: Theme.warn; wrapMode: Text.WordWrap; elide: Text.ElideNone }

    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton {
            text: "Riorganizza"
            primary: true
            enabled: !Workspace.busy
            onClicked: {
                root.error = Workspace.applyTopics(root.topics)
                if (root.error === "")
                    root.close()
            }
        }
    ]
}
