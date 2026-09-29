// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick

Rectangle {
    id: badge

    property string status: ""
    property string text: ""

    readonly property string displayLabel: {
        if (text.length > 0) return text
        if (status === "succeeded") return qsTr("成功")
        if (status === "failed") return qsTr("失败")
        if (status === "cancelled") return qsTr("已取消")
        if (status === "running") return qsTr("生成中")
        if (status === "queued") return qsTr("排队")
        return status
    }

    readonly property color statusColor: {
        if (status === "succeeded") return Theme.success
        if (status === "failed") return Theme.danger
        if (status === "running") return Theme.accent
        if (status === "queued") return Theme.warning
        return Theme.fgMuted
    }

    readonly property color statusBg: {
        if (status === "succeeded") return Theme.successSubtle
        if (status === "failed") return Theme.dangerSubtle
        if (status === "running") return Theme.accentSubtle
        if (status === "queued") return Theme.warningSubtle
        return Theme.surfaceAlt
    }

    implicitHeight: 22
    implicitWidth: rowLayout.implicitWidth + 16
    radius: 11
    color: statusBg
    border.color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.3)
    border.width: 1

    Row {
        id: rowLayout
        anchors.centerIn: parent
        spacing: 5

        // Pulsing dot for "running" state
        Rectangle {
            id: dot
            width: 6
            height: 6
            radius: 3
            color: badge.statusColor
            anchors.verticalCenter: parent.verticalCenter
            visible: badge.status === "running"

            SequentialAnimation on opacity {
                running: badge.status === "running"
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.2; duration: 600; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.2; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
            }
        }

        Text {
            text: badge.displayLabel
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
            color: badge.statusColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
