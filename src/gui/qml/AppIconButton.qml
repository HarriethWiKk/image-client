// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control

    property string toolTipText: ""
    property color customTextColor: Theme.fgMuted
    property color customHoverColor: Theme.fg

    implicitWidth: 28
    implicitHeight: 28
    padding: 0
    hoverEnabled: true

    scale: down ? 0.92 : (hovered ? 1.08 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutQuad }
    }

    ToolTip.text: control.toolTipText
    ToolTip.visible: control.hovered && control.toolTipText.length > 0
    ToolTip.delay: 400

    background: Rectangle {
        radius: control.width / 2
        color: control.down ? Theme.surfaceAlt : (control.hovered ? Theme.surfaceHover : "transparent")
        Behavior on color {
            ColorAnimation { duration: Theme.animFast }
        }
    }

    contentItem: Text {
        text: control.text
        font.family: Theme.fontFamily
        font.pixelSize: 13
        color: control.hovered ? control.customHoverColor : control.customTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
