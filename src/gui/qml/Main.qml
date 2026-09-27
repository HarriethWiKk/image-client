// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Window {
    id: root
    width: 960
    height: 640
    minimumWidth: 640
    visible: true
    title: qsTr("image-client")

    // Placeholder shell: exercises the module set a real view needs (layouts,
    // controls, model-backed list, async image) so the packaged footprint is
    // measured against something representative rather than an empty window.
    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 10

            Label {
                text: qsTr("Generate")
                font.pixelSize: 18
                font.bold: true
            }

            TextField {
                id: promptField
                Layout.fillWidth: true
                placeholderText: qsTr("Prompt")
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: modelBox
                    Layout.fillWidth: true
                    model: ["gpt-image-2", "grok-imagine-image", "gemini-2.5-flash-image"]
                }
                SpinBox {
                    id: countBox
                    from: 1
                    to: 8
                }
            }

            Button {
                text: qsTr("Generate")
                Layout.alignment: Qt.AlignRight
            }

            Item { Layout.fillHeight: true }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.rightMargin: 16
            Layout.bottomMargin: 16
            cellWidth: 220
            cellHeight: 240
            clip: true
            model: 12

            delegate: Item {
                required property int index
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: 6
                    color: "#1f1f1f"
                    border.color: "#3a3a3a"

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 2048
                        cache: true
                        source: "image://provider/placeholder"
                    }

                    Label {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: index + 1
                        color: "#dddddd"
                    }
                }
            }
        }
    }
}
