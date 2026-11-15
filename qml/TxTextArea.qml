import QtQuick
import Texfix

// Un campo di testo su più righe: cresce con il testo, e scorre solo la pagina che lo contiene.
Rectangle {
    id: root
    property alias text: edit.text
    property string placeholder
    property bool mono: false
    signal edited()
    implicitHeight: Math.max(3 * Theme.rowHeight, edit.implicitHeight)
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: edit.activeFocus ? Theme.accent : Theme.border
    TextEdit {
        id: edit
        anchors.fill: parent
        padding: Theme.gap
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
        anchors.fill: parent
        anchors.margins: Theme.gap
        visible: edit.text === "" && root.placeholder !== ""
        text: root.placeholder
        color: Theme.textFaint
        verticalAlignment: Text.AlignTop
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
}
