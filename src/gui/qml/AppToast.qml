// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick

Rectangle {
    id: toast

    property string message: ""
    property string type: "info" // "info", "success", "error"

    readonly property color toastColor: {
        if (type === "success") return Theme.success
        if (type === "error") return Theme.danger
        return Theme.accent
    }

    readonly property color toastBg: Theme.surface
    readonly property color toastBorder: {
        if (type === "success") return Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.4)
        if (type === "error") return Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.4)
        return Theme.borderColor
    }

    readonly property string iconSymbol: {
        if (type === "success") return "✓"
        if (type === "error") return "!"
        return "ℹ"
    }

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    width: Math.min(row.implicitWidth + 32, parent ? (parent.width - 48) : 480)
    implicitHeight: Math.max(38, row.implicitHeight + 16)
    radius: Theme.radius
    color: toastBg
    border.color: toastBorder
    border.width: 1
    z: 9999

    y: -height - 20
    opacity: 0
    visible: opacity > 0

    function show(msg, toastType, durationMs) {
        if (!msg || msg.length === 0) return
        message = msg
        type = toastType || "info"
        dismissTimer.interval = durationMs || 3500
        dismissTimer.restart()
        state = "visible"
    }

    function hide() {
        dismissTimer.stop()
        state = "hidden"
    }

    states: [
        State {
            name: "visible"
            PropertyChanges { target: toast; y: 24; opacity: 1.0 }
        },
        State {
            name: "hidden"
            PropertyChanges { target: toast; y: -toast.height - 20; opacity: 0.0 }
        }
    ]

    transitions: [
        Transition {
            to: "visible"
            ParallelAnimation {
                NumberAnimation { properties: "y"; duration: 250; easing.type: Easing.OutBack }
                NumberAnimation { properties: "opacity"; duration: 200 }
            }
        },
        Transition {
            to: "hidden"
            ParallelAnimation {
                NumberAnimation { properties: "y"; duration: 200; easing.type: Easing.InQuad }
                NumberAnimation { properties: "opacity"; duration: 180 }
            }
        }
    ]

    Timer {
        id: dismissTimer
        repeat: false
        onTriggered: toast.hide()
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10
        width: parent.width - 24

        Rectangle {
            width: 20
            height: 20
            radius: 10
            color: toast.toastColor
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.centerIn: parent
                text: toast.iconSymbol
                color: "#ffffff"
                font.family: Theme.fontFamily
                font.pixelSize: 11
                font.bold: true
            }
        }

        Text {
            id: textItem
            text: toast.message
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.fg
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 44
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
        }

        Text {
            text: "✕"
            font.pixelSize: 11
            color: closeMouse.containsMouse ? Theme.fg : Theme.fgMuted
            anchors.verticalCenter: parent.verticalCenter

            MouseArea {
                id: closeMouse
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: toast.hide()
            }
        }
    }
}
