// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs

// GUI-D lightbox: a modal overlay bound to the Lightbox controller (oic::app::LightboxController).
// All state -- open / currentIndex / zoom / compare / compareIndex -- lives in the controller, so
// this file is presentation + input only. Modal + NoAutoClose keeps keyboard focus inside the popup
// (SPEC 9.1 "lightbox 焦点锁定"). Keys sit on a focusable Item, not the Popup itself -- a Popup is
// not an Item, so the Keys attached property cannot bind to it directly.
Popup {
    id: lightbox
    parent: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    width: parent ? parent.width : 640
    height: parent ? parent.height : 480
    visible: Lightbox.open

    background: Rectangle { color: "#d9000000" }

    function urlAt(index) {
        return (index >= 0 && index < Lightbox.model.length) ? Lightbox.model[index] : ""
    }

    // The lightbox model holds image://asset/<relPath> urls (Main.qml's relToUrl). Save As and
    // open-externally need the store relPath, so strip the provider prefix back off.
    property string assetPrefix: "image://asset/"
    function relAt(index) {
        var url = urlAt(index)
        return url.indexOf(assetPrefix) === 0 ? url.substring(assetPrefix.length) : ""
    }

    property string status: ""
    // "Save As" and "open with the system" (SPEC 9.1: native 另存为 / QDesktopServices). Open is the
    // WebP escape hatch -- Qt cannot decode it in-app (SPEC 6.5), so the OS viewer can.
    function saveCurrent() {
        var rel = relAt(Lightbox.currentIndex)
        if (rel === "") { status = qsTr("无法保存：图片未就绪"); return }
        // QQuickFileDialog (Qt 6.8) has no `defaultName`/`SaveAs`: prefill the name by seeding
        // currentFile (a url) from the asset's own path, and ask the platform for a save dialog.
        saveDialog.currentFile = "file:///" + Backend.assetAbsolutePath(rel).replace(/\\/g, "/")
        saveDialog.open()
    }
    function openCurrentExternally() {
        var rel = relAt(Lightbox.currentIndex)
        if (rel === "") { status = qsTr("无法打开：图片未就绪"); return }
        status = Backend.openExternally(rel) ? "" : Backend.lastError()
    }

    // Focusable surface: holds the keyboard handlers and all content. forceActiveFocus on open so
    // the keys land here regardless of what had focus behind the popup.
    Item {
        id: surface
        anchors.fill: parent
        focus: true
        activeFocusOnTab: true
        onActiveFocusChanged: {
            // If focus ever escapes (Tab), pull it straight back -- the web lightbox leaked focus
            // behind the dialog; a modal popup that re-grabs does not.
            if (!activeFocus && lightbox.visible)
                surface.forceActiveFocus()
        }
        Component.onCompleted: if (visible) forceActiveFocus()

        // Keyboard: Esc close, <-/-> prev/next (wrap), +/-/0 zoom, C compare, S save-as, O open-external.
        Keys.onEscapePressed: Lightbox.close()
        Keys.onLeftPressed: Lightbox.prev()
        Keys.onRightPressed: Lightbox.next()
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { Lightbox.zoomIn(); event.accepted = true }
            else if (event.key === Qt.Key_Minus || event.key === Qt.Key_Underscore) { Lightbox.zoomOut(); event.accepted = true }
            else if (event.key === Qt.Key_0) { Lightbox.resetZoom(); event.accepted = true }
            else if (event.key === Qt.Key_C) { Lightbox.toggleCompare(); event.accepted = true }
            else if (event.key === Qt.Key_S) { lightbox.saveCurrent(); event.accepted = true }
            else if (event.key === Qt.Key_O) { lightbox.openCurrentExternally(); event.accepted = true }
        }

        // Top bar: filename + readout on the left, zoom / compare / close on the right.
        Rectangle {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 40
            color: "#22000000"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                Label {
                    text: lightbox.urlAt(Lightbox.currentIndex)
                    color: "#e8e8e8"; elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Label {
                    visible: lightbox.status.length > 0
                    text: lightbox.status
                    color: "#e0a94a"
                }
                Label {
                    text: (Lightbox.currentIndex + 1) + " / " + Lightbox.count
                          + "   " + Math.round(Lightbox.zoom * 100) + "%"
                          + (Lightbox.canCompare ? "   [C]对比" : "")
                    color: "#9a9a9a"
                }
                Button {
                    text: qsTr("另存为")
                    ToolTip.text: qsTr("原生另存为 [S]")
                    ToolTip.visible: hovered
                    onClicked: lightbox.saveCurrent()
                }
                Button {
                    text: qsTr("用系统打开")
                    ToolTip.text: qsTr("外部程序打开，webp 等无法预览的格式走这里 [O]")
                    ToolTip.visible: hovered
                    onClicked: lightbox.openCurrentExternally()
                }
                Button {
                    text: qsTr("对比")
                    enabled: Lightbox.canCompare
                    highlighted: Lightbox.compare
                    onClicked: Lightbox.toggleCompare()
                }
                Button { text: qsTr("关闭"); onClicked: Lightbox.close() }
            }
        }

        // Image surface: one panel, or two side-by-side in compare mode. Full resolution comes from
        // loading the asset at native size (sourceSize 0 => the provider returns the full image) and
        // scaling by the controller's zoom; wheel/drag adjust and pan.
        Row {
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            spacing: 16

            Repeater {
                model: Lightbox.compare
                    ? [Lightbox.currentIndex, Lightbox.compareIndex]
                    : [Lightbox.currentIndex]
                delegate: Item {
                    id: panel
                    width: (parent.width - (Lightbox.compare ? 16 : 0)) / (Lightbox.compare ? 2 : 1)
                    height: parent.height
                    clip: true

                    Image {
                        id: img
                        anchors.centerIn: parent
                        source: lightbox.urlAt(modelData)
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        width: panel.width
                        height: panel.height
                        scale: Lightbox.zoom
                        sourceSize.width: 0
                        sourceSize.height: 0
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: img.status === Image.Loading
                        visible: running
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton  // let clicks pass; only wheel is consumed here
                        onWheel: (wheel) => {
                            if (wheel.angleDelta.y > 0) Lightbox.zoomIn()
                            else Lightbox.zoomOut()
                        }
                    }
                }
            }
        }
    }

    // Native "Save As". selectedFile is a file:// url that Backend.copyAssetTo accepts.
    FileDialog {
        id: saveDialog
        title: qsTr("另存为")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp)"), qsTr("所有文件 (*)")]
        onAccepted: {
            var rel = lightbox.relAt(Lightbox.currentIndex)
            lightbox.status = (rel !== "" && Backend.copyAssetTo(rel, saveDialog.selectedFile.toString()))
                ? qsTr("已另存为") : Backend.lastError()
        }
    }
}
