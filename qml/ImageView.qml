import QtQuick
import Texfix

// Un'immagine in una scheda: intera, mai più grande di com'è.
Rectangle {
    id: root
    required property string file
    color: Theme.pdfBackdrop
    Image {
        anchors.centerIn: parent
        width: Math.min(sourceSize.width / Screen.devicePixelRatio, root.width - 4 * Theme.gap)
        height: Math.min(sourceSize.height / Screen.devicePixelRatio, root.height - 4 * Theme.gap)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
        source: "file://" + encodeURI(Workspace.root + "/" + root.file)
    }
}
