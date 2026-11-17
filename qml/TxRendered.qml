import QtQuick
import QtQuick.Layouts
import Texfix

// Un pezzo come nel PDF (`Workspace.snippet`): intanto il testo; se non compila resta il testo.
Item {
    id: piece
    property string text
    property string kind: "prosa"                      // prosa | latex
    property string shown: text                        // il testo da mostrare intanto
    property bool mono: false
    property color textColor: Theme.text
    property int weight: Font.Normal
    property bool fit: true                            // sta nella larghezza che ha; altrimenti prende la sua
    property real zoom: 0.52                           // la resa è a 200 punti per pollice
    property int tick: 0
    Connections { target: Workspace; function onSnippetsReady() { piece.tick++ } }
    readonly property string ink: ("" + textColor).slice(-6)
    readonly property string path: text !== "" && tick >= 0 ? Workspace.snippet(text, kind, ink) : ""
    readonly property bool ready: picture.status === Image.Ready
    Layout.fillWidth: true
    implicitWidth: ready ? picture.width : label.implicitWidth
    implicitHeight: ready ? picture.height : label.implicitHeight
    TxLabel {
        id: label
        visible: !piece.ready
        width: parent.width
        text: piece.shown
        color: piece.textColor
        font.weight: piece.weight
        font.family: piece.mono ? Theme.monoFamily : Qt.application.font.family
        wrapMode: piece.mono ? Text.WrapAnywhere : Text.WordWrap
        elide: Text.ElideNone
    }
    Image {
        id: picture
        visible: piece.ready
        source: piece.path !== "" ? "file://" + piece.path : ""
        asynchronous: true
        width: piece.fit ? Math.min(piece.width, sourceSize.width * piece.zoom) : sourceSize.width * piece.zoom
        height: sourceSize.width > 0 ? width * sourceSize.height / sourceSize.width : 0
        smooth: true
        mipmap: true
    }
}
