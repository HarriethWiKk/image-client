// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

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

        // Keyboard: Esc close, <-/-> prev/next (wrap), +/-/0 zoom, C compare.
        Keys.onEscapePressed: Lightbox.close()
        Keys.onLeftPressed: Lightbox.prev()
        Keys.onRightPressed: Lightbox.next()
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { Lightbox.zoomIn(); event.accepted = true }
            else if (event.key === Qt.Key_Minus || event.key === Qt.Key_Underscore) { Lightbox.zoomOut(); event.accepted = true }
            else if (event.key === Qt.Key_0) { Lightbox.resetZoom(); event.accepted = true }
            else if (event.key === Qt.Key_C) { Lightbox.toggleCompare(); event.accepted = true }
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
                    text: (Lightbox.currentIndex + 1) + " / " + Lightbox.count
                          + "   " + Math.round(Lightbox.zoom * 100) + "%"
                          + (Lightbox.canCompare ? "   [C]对比" : "")
                    color: "#9a9a9a"
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
}
