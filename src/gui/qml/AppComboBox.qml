// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control

    implicitHeight: 34
    leftPadding: 10
    rightPadding: 28
    hoverEnabled: true

    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.enabled ? Theme.surfaceAlt : Theme.surface
        border.color: control.activeFocus ? Theme.accent : (control.hovered ? Theme.borderLight : Theme.borderColor)
        border.width: control.activeFocus ? 2 : 1

        Behavior on border.color {
            ColorAnimation { duration: Theme.animFast }
        }
    }

    contentItem: control.editable ? control.editText : textItem

    Text {
        id: textItem
        leftPadding: 2
        rightPadding: control.indicator ? (control.indicator.width + 6) : 20
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.fg : Theme.fgMuted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        visible: !control.editable
    }

    indicator: Text {
        x: control.width - width - 10
        y: (control.height - height) / 2
        text: "▾"
        font.pixelSize: 12
        color: control.hovered ? Theme.fg : Theme.fgMuted
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 240)
        padding: 4
        topMargin: 4
        bottomMargin: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.surface
            border.color: Theme.borderColor
            border.width: 1
        }
    }

    delegate: ItemDelegate {
        id: delegateItem
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: 32
        highlighted: control.highlightedIndex === index
        hoverEnabled: true

        contentItem: Text {
            text: {
                if (control.textRole && typeof model === "object" && model !== null) {
                    return model[control.textRole] !== undefined ? model[control.textRole] : ""
                }
                return (typeof modelData !== "undefined") ? modelData : ""
            }
            color: (control.currentIndex === index) ? Theme.accent : Theme.fg
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.weight: (control.currentIndex === index) ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
            rightPadding: 8
        }

        background: Rectangle {
            radius: Theme.radiusSm - 2
            color: delegateItem.highlighted ? Theme.surfaceHover : (delegateItem.hovered ? Theme.surfaceAlt : "transparent")
        }
    }
}
