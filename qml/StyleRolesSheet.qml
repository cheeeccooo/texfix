import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Texfix

// Le domande all'importazione di uno stile: a che cosa serve ogni ambiente trovato nel .sty. Il ruolo proposto viene
// dal nome; l'utente conferma o cambia.
TxSheet {
    id: root
    title: "Lo stile «" + styleName + "»: a che cosa serve ogni ambiente?"
    wantedWidth: 560
    wantedHeight: 560
    property string styleName
    property var environments: []                      // name, role, color
    property var roles: []
    signal confirmed()

    function ask(name, environments) {
        styleName = name
        roles = Workspace.styleRoles()
        root.environments = environments
        open()
    }
    function labelOf(role) {
        for (const r of roles)
            if (r.value === role)
                return r.label
        return "Da decidere"
    }
    function setRole(index, role) {
        const copy = environments.slice()
        copy[index] = { name: copy[index].name, role: role, color: copy[index].color }
        environments = copy
    }

    TxLabel {
        Layout.fillWidth: true
        text: "Texfix userà ogni ambiente per il suo ruolo. Ciò che non serve agli appunti (ambienti di servizio dello stile) è «Niente di questo»."
        color: Theme.textMuted
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Repeater {
        model: root.environments
        RowLayout {
            id: row
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: Theme.gap
            Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 5; color: row.modelData.color !== "" ? row.modelData.color : "transparent"; border.width: row.modelData.color !== "" ? 0 : 1; border.color: Theme.border }
            TxLabel { Layout.fillWidth: true; text: row.modelData.name; font.family: Theme.monoFamily }
            TxButton {
                text: root.labelOf(row.modelData.role) + "  ▾"
                primary: row.modelData.role === ""
                onClicked: { roleMenu.index = row.index; roleMenu.popup() }
            }
        }
    }
    Menu {
        id: roleMenu
        property int index
        Instantiator {
            model: root.roles
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: root.setRole(roleMenu.index, modelData.value)
            }
            onObjectAdded: (index, object) => roleMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => roleMenu.removeItem(object)
        }
    }

    buttons: [
        TxButton { text: "Annulla"; onClicked: root.close() },
        TxButton {
            text: "Usa questo stile"
            primary: true
            enabled: root.environments.every(e => e.role !== "")
            hint: enabled ? "" : "Resta qualche ambiente da decidere"
            onClicked: {
                const roles = {}
                for (const e of root.environments)
                    roles[e.name] = e.role
                Workspace.setStyleRoles(root.styleName, roles)
                root.close()
                root.confirmed()
            }
        }
    ]
}
