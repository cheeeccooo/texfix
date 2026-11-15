import QtQuick
import QtQuick.Controls
import Texfix

// Un PDF: le pagine una sotto l'altra, larghe quanto il pannello. I collegamenti funzionano; ⌘-clic o doppio clic
// su un punto chiede il sorgente (sourceAsked, in punti della pagina).
Rectangle {
    id: root
    color: Theme.pdfBackdrop
    property alias source: backend.source
    signal sourceAsked(int page, real x, real y)       // page da 1
    readonly property int margin: Theme.gap + 4
    // la larghezza in pixel a cui si disegnano le pagine: segue il pannello con un po' di ritardo, non a ogni pixel
    property int renderWidth: 0
    readonly property int wantedWidth: Math.round(Math.max(0, width - 2 * margin) * Screen.devicePixelRatio)
    onWantedWidthChanged: settle.restart()
    Timer { id: settle; interval: 180; onTriggered: root.renderWidth = root.wantedWidth }
    Component.onCompleted: renderWidth = wantedWidth

    // il punto a cui ha portato l'ultimo salto, segnato per un momento
    property int markPage: 0
    property real markY: 0
    property real markHeight: 0

    function reload() { backend.reload() }
    // Porta a un punto di una pagina (page da 1, y in punti) e lo segna.
    function show(page, y, height) {
        if (page < 1 || page > backend.pageCount)
            return
        const size = backend.pageSize(page - 1)
        const scale = size.width > 0 ? pages.width / size.width : 1
        pages.positionViewAtIndex(page - 1, ListView.Beginning)
        const top = pages.contentY + y * scale - pages.height / 3
        pages.contentY = Math.max(pages.originY - pages.topMargin, Math.min(top, pages.originY + pages.contentHeight + pages.bottomMargin - pages.height))
        markPage = page
        markY = y
        markHeight = Math.max(height, 8)
        fadeMark.restart()
    }
    Timer { id: fadeMark; interval: 1600; onTriggered: root.markPage = 0 }

    PdfBackend { id: backend }

    ListView {
        id: pages
        anchors.fill: parent
        anchors.leftMargin: root.margin
        anchors.rightMargin: root.margin
        topMargin: root.margin
        bottomMargin: root.margin
        spacing: root.margin
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 1200
        model: backend.pageCount
        ScrollBar.vertical: ScrollBar { parent: root; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom }
        delegate: Rectangle {
            id: page
            required property int index
            readonly property size points: backend.revision >= 0 ? backend.pageSize(index) : Qt.size(1, 1)
            readonly property real scale: points.width > 0 ? width / points.width : 1
            width: pages.width
            height: points.width > 0 ? Math.round(width * points.height / points.width) : width * 1.414
            color: "white"
            Image {
                anchors.fill: parent
                asynchronous: true
                retainWhileLoading: true
                cache: false
                source: root.renderWidth > 0 && backend.revision > 0 ? backend.pageUrl(page.index, root.renderWidth) : ""
            }
            Rectangle {
                visible: opacity > 0
                opacity: root.markPage === page.index + 1 ? 0.35 : 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
                y: root.markY * page.scale - 2
                width: parent.width
                height: root.markHeight * page.scale + 4
                color: Theme.accent
            }
            TapHandler {
                acceptedModifiers: Qt.KeyboardModifierMask
                onTapped: (point, button) => {
                    const x = point.position.x / page.scale, y = point.position.y / page.scale
                    if (point.modifiers & Qt.ControlModifier)
                        return root.sourceAsked(page.index + 1, x, y)
                    const link = backend.linkAt(page.index, x, y)
                    if (!link.valid)
                        return
                    if (link.url !== "")
                        Qt.openUrlExternally(link.url)
                    else if (link.page >= 0)
                        root.show(link.page + 1, link.y, 0)
                }
                onDoubleTapped: point => root.sourceAsked(page.index + 1, point.position.x / page.scale, point.position.y / page.scale)
            }
        }
    }
    TxLabel {
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 4 * Theme.gap)
        visible: backend.pageCount === 0
        text: "Compila per vedere il PDF."
        color: Theme.textFaint
    }
}
