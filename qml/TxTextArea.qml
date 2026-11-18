import QtQuick
import Texfix

// Un campo di testo su più righe: cresce con il testo, e scorre solo la pagina che lo contiene.
Rectangle {
    id: root
    property alias text: edit.text
    property string placeholder
    property bool mono: false
    property bool readOnly: false
    property real minHeight: 3 * Theme.rowHeight         // un'istruzione parte da una riga, e cresce con il testo
    signal edited()
    function focusEnd() { edit.forceActiveFocus(); edit.cursorPosition = edit.length }
    implicitHeight: Math.max(minHeight, edit.implicitHeight, hint.visible ? hint.implicitHeight + 2 * edit.padding : 0)
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: edit.activeFocus ? Theme.accent : Theme.border
    TextEdit {
        id: edit
        anchors.fill: parent
        padding: root.minHeight < 2 * Theme.rowHeight ? 6 : Theme.gap
        readOnly: root.readOnly
        color: Theme.text
        selectionColor: Theme.selection
        selectedTextColor: Theme.text
        font.pixelSize: Theme.textSize
        font.family: root.mono ? Theme.monoFamily : Qt.application.font.family
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        selectByMouse: true
        onTextChanged: if (activeFocus) root.edited()
    }
    TxLabel {
        id: hint
        x: edit.padding; y: edit.padding
        width: parent.width - 2 * edit.padding
        visible: edit.text === "" && root.placeholder !== ""
        text: root.placeholder
        color: Theme.textFaint
        verticalAlignment: Text.AlignTop
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
}
