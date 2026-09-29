// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control

    // Variants: "primary", "secondary", "ghost", "danger"
    property string variant: "secondary"
    property real customRadius: Theme.radiusSm
    property alias labelFont: textItem.font

    implicitHeight: 34
    implicitWidth: Math.max(76, textItem.implicitWidth + 24)
    padding: 0
    hoverEnabled: true

    scale: down ? 0.97 : (hovered && enabled ? 1.01 : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutQuad }
    }

    background: Rectangle {
        radius: control.customRadius
        color: {
            if (!control.enabled)
                return Theme.surfaceAlt
            if (control.variant === "primary")
                return control.down ? Theme.accentPressed : (control.hovered ? Theme.accentHover : Theme.accent)
            if (control.variant === "danger")
                return control.down ? Theme.danger : (control.hovered ? Theme.dangerHover : Theme.dangerSubtle)
            if (control.variant === "ghost")
                return control.down ? Theme.surfaceAlt : (control.hovered ? Theme.surfaceHover : "transparent")
            // "secondary" default
            return control.down ? Theme.surfaceAlt : (control.hovered ? Theme.surfaceHover : Theme.surface)
        }
        border.color: {
            if (!control.enabled)
                return "transparent"
            if (control.variant === "primary")
                return "transparent"
            if (control.variant === "danger")
                return control.hovered ? Theme.danger : Theme.borderColor
            if (control.variant === "ghost")
                return "transparent"
            return control.hovered ? Theme.borderLight : Theme.borderColor
        }
        border.width: 1

        Behavior on color {
            ColorAnimation { duration: Theme.animFast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.animFast }
        }
    }

    contentItem: Text {
        id: textItem
        text: control.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.weight: (control.variant === "primary") ? Font.DemiBold : Font.Normal
        color: {
            if (!control.enabled)
                return Theme.fgSubtle
            if (control.variant === "primary")
                return Theme.accentFg
            if (control.variant === "danger")
                return control.hovered ? Theme.dangerFg : Theme.danger
            return control.down ? Theme.fg : (control.hovered ? Theme.fg : (control.variant === "ghost" ? Theme.fgMuted : Theme.fg))
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
