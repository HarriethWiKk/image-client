// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick

Rectangle {
    id: card

    property bool hoverable: false
    readonly property bool hovered: hoverable && mouseArea.containsMouse

    color: Theme.surface
    border.color: hovered ? Theme.borderLight : Theme.borderColor
    border.width: 1
    radius: Theme.radius

    Behavior on border.color {
        ColorAnimation { duration: Theme.animFast }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: card.hoverable
        hoverEnabled: card.hoverable
        cursorShape: card.hoverable ? Qt.PointingHandCursor : Qt.ArrowCursor
        acceptedButtons: Qt.NoButton  // does not consume clicks by default unless desired
    }
}
