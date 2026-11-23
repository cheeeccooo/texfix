import QtQuick
import QtQuick.Layouts
import Texfix

// Come i lavori usano Claude, per tutto il programma: la velocità (quante chiamate e agenti insieme) e il tetto (fin dove
// un lavoro può arrivare in una finestra di utilizzo, per lasciare margine alla chat). Si cambiano anche mentre un
// lavoro gira. Lo usano il pannello dell'utilizzo in alto e la finestra del lavoro.
ColumnLayout {
    id: root
    property bool compact: false                       // nella finestra del lavoro: una riga sola, senza le spiegazioni
    spacing: Theme.gap

    component Hint: TxLabel {
        visible: !root.compact
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        color: Theme.textMuted
        font.pixelSize: Theme.smallSize
    }

    Flow {
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel { text: "Velocità dei lavori"; height: Theme.rowHeight; verticalAlignment: Text.AlignVCenter; font.weight: Font.Medium }
        TxSegmented {
            choices: [{ value: "piano", label: "Piano" }, { value: "normale", label: "Normale" }, { value: "veloce", label: "Veloce" }]
            value: Workspace.pace
            onChosen: value => Workspace.pace = value
        }
        Item { width: root.compact ? Theme.gap * 2 : 0; height: 1; visible: root.compact }
        TxLabel { visible: root.compact; text: "Tetto"; height: Theme.rowHeight; verticalAlignment: Text.AlignVCenter; font.weight: Font.Medium }
        Row {
            visible: root.compact
            spacing: 4
            TxSegmented {
                choices: [{ value: "no", label: "No" }, { value: "si", label: Workspace.capPercent + "%" }]
                value: Workspace.capOn ? "si" : "no"
                onChosen: value => Workspace.capOn = value === "si"
            }
            TxButton { text: "−"; enabled: Workspace.capOn && Workspace.capPercent > 10; hint: "Il tetto più basso di 5"; onClicked: Workspace.capPercent -= 5 }
            TxButton { text: "+"; enabled: Workspace.capOn && Workspace.capPercent < 95; hint: "Il tetto più alto di 5"; onClicked: Workspace.capPercent += 5 }
        }
    }
    Hint {
        text: Workspace.pace === "piano" ? "Piano: 2 chiamate e un agente alla volta. Il lavoro dura di più e consuma la finestra più lentamente; il totale è lo stesso."
              : Workspace.pace === "normale" ? "Normale: 4 chiamate e un agente alla volta."
              : "Veloce: 6 chiamate e due agenti insieme. Il lavoro finisce prima, ma consuma la finestra più in fretta."
    }

    RowLayout {
        visible: !root.compact
        Layout.fillWidth: true
        spacing: Theme.gap
        TxLabel { text: "Tetto dei lavori"; font.weight: Font.Medium }
        TxSegmented {
            choices: [{ value: "no", label: "Spento" }, { value: "si", label: "Acceso" }]
            value: Workspace.capOn ? "si" : "no"
            onChosen: value => Workspace.capOn = value === "si"
        }
        TxButton { text: "−"; enabled: Workspace.capOn && Workspace.capPercent > 10; hint: "Il tetto più basso di 5"; onClicked: Workspace.capPercent -= 5 }
        TxLabel { text: Workspace.capPercent + "%"; color: Workspace.capOn ? Theme.text : Theme.textMuted; font.weight: Font.Medium }
        TxButton { text: "+"; enabled: Workspace.capOn && Workspace.capPercent < 95; hint: "Il tetto più alto di 5"; onClicked: Workspace.capPercent += 5 }
        Item { Layout.fillWidth: true }
    }
    Hint {
        text: Workspace.capOn ? "Un lavoro si ferma quando una finestra di utilizzo (le cinque ore o la settimana) arriva al " + Workspace.capPercent
                                + "%, e riparte da sé quando si azzera. Il resto della finestra rimane per la chat e per te."
                              : "Senza tetto un lavoro usa la finestra fino al limite di Claude, poi va in pausa e riparte da sé quando si azzera."
    }
}
