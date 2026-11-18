import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Texfix

// Il piano di un lavoro, come documento: il riepilogo, le voci con la spunta e le istruzioni sotto, le domande, il
// commento. Ogni modifica è salvata subito. Un tasto solo: «Avvia», o «Rivedi piano» se c'è un'istruzione.
Rectangle {
    id: root
    required property string planId
    property var plan: ({ sezioni: [], domande: [] })
    property bool instructions: false
    property bool confirmDiscard: false
    signal closeAsked()
    signal chatAsked()
    color: Theme.window

    function reload() {
        plan = Workspace.plan(planId)
        instructions = Workspace.planHasInstructions(planId)
    }
    function store() {
        Workspace.savePlan(planId, plan)
        instructions = Workspace.planHasInstructions(planId)
    }
    function setEntry(s, v, key, value) { plan.sezioni[s].voci[v][key] = value; store() }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections { target: Workspace; function onPlanReady(id) { if (id === root.planId) root.reload() } }

    readonly property bool editable: plan.stato === "piano"

    TxScroll {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 4 * Theme.gap
        ColumnLayout {
            id: column
            x: Theme.gap + 4
            y: Theme.gap + 4
            width: Math.min(root.width - 2 * Theme.gap - 8, 860)
            spacing: Theme.gap

            TxLabel { Layout.fillWidth: true; text: root.plan.titolo || "Questo lavoro non c'è più."; font.pixelSize: Theme.textSize + 4; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; elide: Text.ElideNone }
            TxLabel {
                Layout.fillWidth: true
                text: root.plan.stato === "piano" ? "Piano da approvare" : root.plan.stato === "in_corso" ? "L'agente lo sta eseguendo" : root.plan.stato === "esito" ? "Eseguito" : ""
                color: root.plan.stato === "piano" ? Theme.warn : Theme.textMuted
                font.pixelSize: Theme.smallSize
            }
            // l'esito, quando c'è
            Rectangle {
                Layout.fillWidth: true
                visible: root.plan.stato === "esito"
                implicitHeight: outcome.implicitHeight + 2 * Theme.gap
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                Text {
                    id: outcome
                    x: Theme.gap; y: Theme.gap
                    width: parent.width - 2 * Theme.gap
                    text: root.plan.esito || ""
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: Theme.textSize
                }
            }
            TxLabel { Layout.fillWidth: true; visible: text !== ""; text: root.plan.riepilogo || ""; wrapMode: Text.WordWrap; elide: Text.ElideNone }

            Repeater {
                model: root.plan.sezioni
                ColumnLayout {
                    id: section
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 6
                    TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; text: section.modelData.titolo || ""; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                    // una scelta per tutta la sezione (una sovrapposizione, una versione): un tasto per opzione
                    Flow {
                        Layout.fillWidth: true
                        visible: !!section.modelData.scelta
                        spacing: Theme.gap
                        Repeater {
                            model: section.modelData.scelta ? section.modelData.scelta.opzioni : []
                            TxButton {
                                required property string modelData
                                required property int index
                                readonly property string value: section.modelData.scelta.valori[index]
                                readonly property bool on: section.modelData.scelta.valore === value
                                text: (on ? "✓ " : "") + modelData
                                primary: on
                                enabled: root.editable
                                width: Math.min(implicitWidth, column.width)
                                onClicked: { root.plan.sezioni[section.index].scelta.valore = value; root.store(); root.reload() }
                            }
                        }
                    }
                    Repeater {
                        model: section.modelData.voci
                        ColumnLayout {
                            id: entry
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap
                                Rectangle {
                                    implicitWidth: 18; implicitHeight: 18; radius: 4
                                    Layout.alignment: Qt.AlignTop
                                    color: entry.modelData.fare ? Theme.accent : Theme.surface
                                    border.width: 1
                                    border.color: entry.modelData.fare ? Theme.accent : Theme.border
                                    Text { anchors.centerIn: parent; visible: entry.modelData.fare; text: "✓"; color: Theme.onAccent; font.pixelSize: 12; font.weight: Font.Bold }
                                    MouseArea { anchors.fill: parent; enabled: root.editable; onClicked: { root.setEntry(section.index, entry.index, "fare", !entry.modelData.fare); root.reload() } }
                                }
                                TxLabel { Layout.fillWidth: true; text: entry.modelData.testo; wrapMode: Text.WordWrap; elide: Text.ElideNone; opacity: entry.modelData.fare ? 1 : 0.5 }
                            }
                            TextInput {
                                Layout.fillWidth: true
                                Layout.leftMargin: 18 + Theme.gap
                                visible: root.editable || text !== ""
                                readOnly: !root.editable
                                text: entry.modelData.nota
                                color: Theme.textMuted
                                font.pixelSize: Theme.smallSize
                                selectByMouse: true
                                clip: true
                                onTextEdited: root.setEntry(section.index, entry.index, "nota", text)
                                TxLabel { anchors.fill: parent; visible: parent.text === "" && root.editable; text: "↳ istruzioni per questa voce…"; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
                            }
                        }
                    }
                }
            }

            Repeater {
                model: root.plan.domande
                ColumnLayout {
                    id: question
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.gap
                    spacing: 6
                    TxLabel { Layout.fillWidth: true; text: question.modelData.testo; font.weight: Font.Medium; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        Repeater {
                            model: question.modelData.opzioni
                            TxButton {
                                required property string modelData
                                text: (question.modelData.risposta === modelData ? "✓ " : "") + modelData
                                primary: question.modelData.risposta === modelData
                                enabled: root.editable
                                width: Math.min(implicitWidth, column.width)
                                onClicked: { root.plan.domande[question.index].risposta = modelData; root.store(); root.reload() }
                            }
                        }
                    }
                }
            }

            // il piano del materiale: come si scrive (le impostazioni del progetto) e quanto costa, all'incirca
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gap
                visible: root.plan.tipo === "materiale" && root.editable
                spacing: Theme.gap
                TxLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    color: Theme.textMuted
                    font.pixelSize: Theme.smallSize
                    text: (root.plan.impostazioni || "") + (root.plan.stima > 0 ? "\nCosto stimato per scrivere le lezioni nuove: circa " + Number(root.plan.stima).toLocaleString(Qt.locale("it_IT"), "f", 2)
                                                                                  + " $, più il lavoro dell'agente dove si fonde." : "")
                }
                TxButton { text: "Cambia"; hint: "Le impostazioni del progetto (⌘,)"; onClicked: Workspace.askSettings() }
            }

            TxLabel { Layout.fillWidth: true; Layout.topMargin: Theme.gap; visible: root.editable; text: "Commento"; font.weight: Font.Medium }
            TxTextArea {
                Layout.fillWidth: true
                visible: root.editable
                placeholder: "Che cosa cambieresti nel piano?"
                Component.onCompleted: text = root.plan.commento || ""
                onEdited: { root.plan.commento = text; root.store() }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gap
                spacing: Theme.gap
                TxButton {
                    visible: root.plan.stato !== "esito" && root.plan.titolo !== undefined
                    text: root.confirmDiscard ? "Sì, butta il piano" : "Butta il piano"
                    onClicked: {
                        if (!root.confirmDiscard)
                            return root.confirmDiscard = true
                        Workspace.discardPlan(root.planId)
                        root.closeAsked()
                    }
                }
                Item { Layout.fillWidth: true }
                TxButton {
                    visible: root.editable
                    text: root.instructions ? "Rivedi piano" : "Avvia"
                    primary: true
                    // un piano con una domanda senza risposta non parte: l'agente si fermerebbe a chiederla
                    readonly property bool answered: (root.plan.domande || []).every(q => (q.risposta || "") !== "")
                    enabled: !Workspace.busy && (root.instructions || answered)
                    hint: enabled || Workspace.busy ? "" : "Rispondi prima alle domande del piano"
                    onClicked: {
                        if (root.plan.tipo === "materiale") {     // un piano di Texfix, non dell'agente
                            if (root.instructions)
                                Workspace.reviseSortPlan(root.planId)
                            else
                                Workspace.startSortPlan(root.planId)
                            return root.closeAsked()
                        }
                        root.chatAsked()
                        Chat.send(Workspace.planMessage(root.planId, root.instructions ? "rivedi" : "avvia"))
                        if (root.instructions)
                            root.closeAsked()          // il piano rifatto arriva come lavoro nuovo
                        else
                            root.reload()
                    }
                }
                TxButton {
                    visible: root.plan.stato === "esito"
                    text: "Chiudi e archivia"
                    primary: true
                    onClicked: { Workspace.archivePlan(root.planId); root.closeAsked() }
                }
            }
        }
    }
}
