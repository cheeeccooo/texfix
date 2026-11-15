import QtQuick
import Texfix

// Un testo dell'interfaccia: ha sempre una larghezza, e se non ci sta si accorcia con «…».
Text {
    color: Theme.text
    font.pixelSize: Theme.textSize
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
    textFormat: Text.PlainText
}
