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

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 150; easing.type: Easing.OutQuad }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 120; easing.type: Easing.InQuad }
    }

    background: Rectangle {
        color: Theme.dark ? "#eb0a0a0d" : "#e6111114"
    }

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

        // Top bar: Glassmorphic bar with readout, zoom, compare, save & close actions
        Rectangle {
            id: topBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.topBarHeight
            color: Theme.dark ? "#881a1a1e" : "#ccffffff"
            border.color: Theme.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Text {
                    text: lightbox.urlAt(Lightbox.currentIndex)
                    color: Theme.dark ? "#ededf0" : "#18181b"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Text {
                    visible: lightbox.status.length > 0
                    text: lightbox.status
                    color: Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                Rectangle {
                    implicitHeight: 24
                    implicitWidth: infoRow.implicitWidth + 14
                    radius: 12
                    color: Theme.surfaceAlt
                    border.color: Theme.borderColor
                    border.width: 1

                    Row {
                        id: infoRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: (Lightbox.currentIndex + 1) + " / " + Lightbox.count
                            color: Theme.fg
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.bold: true
                        }
                        Text {
                            text: "·  " + Math.round(Lightbox.zoom * 100) + "%"
                            color: Theme.fgMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }

                AppButton {
                    variant: "secondary"
                    implicitHeight: 30
                    text: qsTr("另存为 [S]")
                    onClicked: lightbox.saveCurrent()
                }

                AppButton {
                    variant: "secondary"
                    implicitHeight: 30
                    text: qsTr("系统打开 [O]")
                    ToolTip.text: qsTr("调用外部看图程序打开 (支持 WebP 完整预览)")
                    ToolTip.visible: hovered
                    onClicked: lightbox.openCurrentExternally()
                }

                AppButton {
                    variant: Lightbox.compare ? "primary" : "secondary"
                    implicitHeight: 30
                    text: qsTr("对比 [C]")
                    enabled: Lightbox.canCompare
                    onClicked: Lightbox.toggleCompare()
                }

                AppIconButton {
                    text: "✕"
                    implicitWidth: 30
                    implicitHeight: 30
                    toolTipText: qsTr("关闭灯箱 [Esc]")
                    onClicked: Lightbox.close()
                }
            }
        }

        // Image surface: one panel, or two side-by-side in compare mode
        Row {
            anchors.top: topBar.bottom
            anchors.bottom: bottomHintBar.top
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
                        acceptedButtons: Qt.NoButton
                        onWheel: (wheel) => {
                            if (wheel.angleDelta.y > 0) Lightbox.zoomIn()
                            else Lightbox.zoomOut()
                        }
                    }
                }
            }
        }

        // Bottom shortcut guide bar
        Rectangle {
            id: bottomHintBar
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 12
            height: 28
            width: hintRow.implicitWidth + 24
            radius: 14
            color: Theme.dark ? "#881a1a1e" : "#d0ffffff"
            border.color: Theme.borderColor
            border.width: 1

            Row {
                id: hintRow
                anchors.centerIn: parent
                spacing: 12

                Text {
                    text: "Esc 退出"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.fgMuted
                }
                Text { text: "·"; color: Theme.borderColor }
                Text {
                    text: "← / → 翻页"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.fgMuted
                }
                Text { text: "·"; color: Theme.borderColor }
                Text {
                    text: "+ / - 缩放"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.fgMuted
                }
                Text { text: "·"; color: Theme.borderColor }
                Text {
                    text: "C 双图对比"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.fgMuted
                }
                Text { text: "·"; color: Theme.borderColor }
                Text {
                    text: "S 另存为"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.fgMuted
                }
                Text { text: "·"; color: Theme.borderColor }
                Text {
                    text: "O 系统查看"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.fgMuted
                }
            }
        }
    }

    // Native "Save As"
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
