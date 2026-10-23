// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control

    implicitHeight: 34
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    color: Theme.fg
    selectedTextColor: Theme.accentFg
    selectionColor: Theme.accent
    placeholderTextColor: Theme.fgSubtle
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.enabled ? Theme.surfaceAlt : Theme.surface
        border.color: control.activeFocus ? Theme.accent : (control.hovered ? Theme.borderLight : Theme.borderColor)
        border.width: control.activeFocus ? 2 : 1

        Behavior on border.color {
            ColorAnimation { duration: Theme.animFast }
        }
    }
}
