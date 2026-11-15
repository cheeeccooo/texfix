import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Texfix

// Le domande al cambio di stile: gli ambienti usati nelle lezioni che nel nuovo stile non hanno un corrispondente.
// Per ognuno si sceglie l'ambiente del nuovo stile che lo sostituisce, o di lasciarlo com'è.
TxSheet {
    id: root
    title: styleName === "" ? "Tornare a nessuno stile: che cosa fare di questi ambienti?" : "Passare a «" + styleName + "»: che cosa fare di questi ambienti?"
    wantedWidth: 560
    property string styleName
    property var questions: []                         // name, role, options
    property var choices: ({})                         // ambiente → scelta («=»: resta com'è)

    function ask(name, questions) {
        styleName = name
        root.questions = questions
        const start = {}
        for (const q of questions)
            start[q.name] = "="
        choices = start
        open()
    }
    function choose(name, value) {
        const all = Object.assign({}, choices)
        all[name] = value
        choices = all
    }

    TxLabel {
        Layout.fillWidth: true
        text: "Il nuovo stile non ha un ambiente per questi ruoli. Ciò che resta com'è potrebbe non compilare finché non lo sistemi."
        color: Theme.textMuted
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Repeater {
        model: root.questions
        RowLayout {
            id: row
            required property var modelData
            Layout.fillWidth: true
            spacing: Theme.gap
            TxLabel { text: row.modelData.name; font.family: Theme.monoFamily }
            TxLabel { Layout.fillWidth: true; text: row.modelData.role; color: Theme.textFaint; font.pixelSize: Theme.smallSize }
            TxButton {
                text: (root.choices[row.modelData.name] === "=" ? "Resta com'è" : "Diventa " + root.choices[row.modelData.name]) + "  ▾"
                onClicked: { menu.target = row.modelData.name; menu.options = row.modelData.options; menu.popup() }
            }
        }
    }
    Menu {
        id: menu
        property string target
        property var options: []
        MenuItem { text: "Resta com'è"; onTriggered: root.choose(menu.target, "=") }
        Instantiator {
            model: menu.options
            delegate: MenuItem {
                required property string modelData
                text: "Diventa " + modelData
                onTriggered: root.choose(menu.target, modelData)
            }
            onObjectAdded: (index, object) => menu.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }

    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton { text: "Cambia stile"; primary: true; onClicked: { root.close(); Workspace.applyStyleWith(root.styleName, root.choices) } }
    ]
}
