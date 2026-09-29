// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root

    property alias text: textArea.text
    property alias placeholderText: textArea.placeholderText
    property alias wrapMode: textArea.wrapMode
    property alias readOnly: textArea.readOnly
    property alias textArea: textArea
    property int maxChars: 0
    property string hintText: ""

    color: Theme.surfaceAlt
    radius: Theme.radiusSm
    border.color: textArea.activeFocus ? Theme.accent : (mouseArea.containsMouse ? Theme.borderLight : Theme.borderColor)
    border.width: textArea.activeFocus ? 2 : 1

    Behavior on border.color {
        ColorAnimation { duration: Theme.animFast }
    }

    function forceActiveFocus() {
        textArea.forceActiveFocus()
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
        acceptedButtons: Qt.NoButton
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 4
        anchors.bottomMargin: (root.maxChars > 0 || root.hintText.length > 0) ? 22 : 4
        clip: true

        TextArea {
            id: textArea
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.fg
            selectedTextColor: Theme.accentFg
            selectionColor: Theme.accent
            placeholderTextColor: Theme.fgSubtle
            wrapMode: TextArea.Wrap
            background: null
            padding: 6
        }
    }

    // Bottom status bar: hint badges and character count
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 20
        anchors.margins: 4
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        visible: root.maxChars > 0 || root.hintText.length > 0

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.hintText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            color: Theme.fgSubtle
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.maxChars > 0
            text: textArea.text.length + " / " + root.maxChars
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            color: (root.maxChars > 0 && textArea.text.length > root.maxChars) ? Theme.danger : Theme.fgSubtle
        }
    }
}
