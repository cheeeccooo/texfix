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

    // rileggere ricrea le righe: la pagina resta dov'era (il contenuto torna alla sua altezza dopo, e intanto lo
    // scorrimento si riporterebbe in cima)
    function reload() {
        const y = scroll.contentY
        plan = Workspace.plan(planId)
        instructions = Workspace.planHasInstructions(planId)
        Qt.callLater(() => scroll.contentY = Math.max(0, Math.min(y, scroll.contentHeight - scroll.height)))
    }
    // ogni modifica passa da Workspace, che la salva sul piano: qui il piano si rilegge e basta
    function setEntry(s, v, key, value) { Workspace.setPlanEntry(planId, s, v, key, value); instructions = Workspace.planHasInstructions(planId) }
    Component.onCompleted: reload()
    onVisibleChanged: if (visible) reload()
    Connections {
        target: Workspace
        function onPlanReady(id) { if (id === root.planId) Qt.callLater(root.reload) }
        // dopo il gestore del clic: rileggere ricrea le righe, anche quella che ha ricevuto il clic
        function onPlanEdited(id) { if (id === root.planId) Qt.callLater(root.reload) }
    }

    readonly property bool editable: plan.stato === "piano"
    // le domande senza risposta: quelle del piano, e ciò che una lezione nuova riprende (se la sua voce resta)
    readonly property bool answered: (plan.domande || []).every(q => (q.risposta || "") !== "")
                                     && (plan.sezioni || []).every(s => !s.intreccio || s.intreccio.valore !== "" || !(s.voci || []).some(v => v.fare))

    // una scelta di una sezione: un testo, se ha una domanda, e un tasto per opzione
    component SectionChoice: ColumnLayout {
        id: sectionChoice
        property string field
        property int section
        property var choice
        Layout.fillWidth: true
        visible: !!choice
        spacing: 4
        TxLabel {
            Layout.fillWidth: true
            visible: !!sectionChoice.choice && !!sectionChoice.choice.testo
            text: sectionChoice.choice && sectionChoice.choice.testo ? sectionChoice.choice.testo : ""
            color: sectionChoice.choice && sectionChoice.choice.valore === "" ? Theme.warn : Theme.text
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gap
            Repeater {
                model: sectionChoice.choice ? sectionChoice.choice.opzioni : []
                TxButton {
                    required property string modelData
                    required property int index
                    readonly property string value: sectionChoice.choice.valori[index]
                    readonly property bool on: sectionChoice.choice.valore === value
                    text: (on ? "✓ " : "") + modelData
                    primary: on
                    enabled: root.editable
                    width: Math.min(implicitWidth, column.width)
                    onClicked: Workspace.setPlanChoice(root.planId, sectionChoice.section, sectionChoice.field, value)
                }
            }
        }
    }

    // il piano di Aggiungi materiale per concetti ha la sua vista
    readonly property bool byConcepts: plan.formato === "concetti"
    Loader {
        id: concepts
        anchors.fill: parent
        active: root.byConcepts
        Component.onCompleted: setSource("ConceptPlan.qml", { planId: root.planId })
    }
    Connections { target: concepts.item; ignoreUnknownSignals: true; function onCloseAsked() { root.closeAsked() } }

    TxScroll {
        id: scroll
        anchors.fill: parent
        visible: !root.byConcepts
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
                text: root.plan.stato === "piano" ? "Piano da approvare" : root.plan.stato === "in_corso" ? "Avviato: si segue nella finestra del lavoro" : root.plan.stato === "esito" ? "Eseguito" : ""
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
                    // una scelta per tutta la sezione (una sovrapposizione, una versione, pezzi da unire)
                    SectionChoice { field: "scelta"; section: section.index; choice: section.modelData.scelta }
                    // ciò che una lezione nuova riprende di una che c'è: una domanda, senza risposta già scelta
                    SectionChoice { field: "intreccio"; section: section.index; choice: section.modelData.intreccio }
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
                                    MouseArea { anchors.fill: parent; enabled: root.editable; onClicked: root.setEntry(section.index, entry.index, "fare", !entry.modelData.fare) }
                                }
                                TxLabel { Layout.fillWidth: true; text: entry.modelData.testo; wrapMode: Text.WordWrap; elide: Text.ElideNone; opacity: entry.modelData.fare ? 1 : 0.5 }
                            }
                            TxTextArea {                // va a capo e cresce con il testo
                                Layout.fillWidth: true
                                Layout.leftMargin: 18 + Theme.gap
                                visible: root.editable || text !== ""
                                readOnly: !root.editable
                                minHeight: Theme.rowHeight + 6
                                placeholder: "Istruzioni per questa voce…"
                                Component.onCompleted: text = entry.modelData.nota || ""
                                onEdited: root.setEntry(section.index, entry.index, "nota", text)
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
                                onClicked: Workspace.setPlanAnswer(root.planId, question.index, modelData)
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
                    text: (root.plan.impostazioni || "") + (root.plan.stima >= 0.01 ? "\nCosto stimato per scrivere le lezioni nuove: circa " + Number(root.plan.stima).toLocaleString(Qt.locale("it_IT"), "f", 2)
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
                onEdited: { Workspace.setPlanComment(root.planId, text); root.instructions = Workspace.planHasInstructions(root.planId) }
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
                TxLabel {                              // perché il tasto è spento, in vista e non solo nel suggerimento
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    color: Theme.warn
                    font.pixelSize: Theme.smallSize
                    text: !root.editable ? "" : Workspace.busy ? "Aspetta che finisca il lavoro in corso" : !root.instructions && !root.answered ? "Rispondi alle domande in giallo" : ""
                }
                TxButton {
                    visible: root.editable
                    text: root.instructions ? "Rivedi piano" : "Avvia"
                    primary: true
                    // un piano con una domanda senza risposta non parte: l'agente si fermerebbe a chiederla
                    enabled: !Workspace.busy && (root.instructions || root.answered)
                    hint: Workspace.busy ? "Aspetta che finisca il lavoro in corso" : enabled ? "" : "Rispondi prima alle domande del piano"
                    onClicked: {
                        if (!root.instructions) {          // il piano si esegue come un lavoro, nella sua finestra
                            Workspace.startSortPlan(root.planId)
                            return root.closeAsked()
                        }
                        if (root.plan.tipo === "materiale")     // un piano di Texfix: lo rifà Texfix
                            Workspace.reviseSortPlan(root.planId)
                        else {                                  // un piano dell'agente: lo rifà l'agente, nella chat
                            root.chatAsked()
                            Chat.send(Workspace.planRevision(root.planId))
                        }
                        root.closeAsked()              // il piano rifatto arriva come lavoro nuovo
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
