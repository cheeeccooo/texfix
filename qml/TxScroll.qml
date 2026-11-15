import QtQuick
import QtQuick.Controls
import Texfix

// Una pagina che scorre: solo in verticale, ferma ai bordi, senza rimbalzi.
Flickable {
    id: root
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    contentWidth: width
    ScrollBar.vertical: ScrollBar { policy: root.contentHeight > root.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
}
