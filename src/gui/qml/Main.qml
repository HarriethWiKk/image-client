// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// GUI-B vertical slice: app shell (left nav + stacked views) and a working Generate
// view bound to the C++ controllers (Jobs / Profiles) exposed by main.cpp as context
// properties. Results render through the image://asset provider (SPEC 6.4-safe decode).
// History/Settings are placeholders until GUI-E/F.
Window {
    id: root
    width: 1120
    height: 760
    minimumWidth: 800
    minimumHeight: 560
    visible: true
    title: qsTr("image-client")

    // Inline dark palette; extracted to a Theme singleton once History/Settings become
    // their own files (GUI-E/F).
    readonly property color bg: "#141414"
    readonly property color surface: "#1e1e1e"
    readonly property color surfaceAlt: "#282828"
    readonly property color borderColor: "#3a3a3a"
    readonly property color fg: "#e8e8e8"
    readonly property color fgMuted: "#9a9a9a"
    readonly property color accent: "#6aa9ff"
    readonly property color accentFg: "#08182e"  // text drawn on an accent-filled surface
    readonly property color danger: "#e0655f"
    readonly property real radius: 10
    readonly property real gap: 12
    readonly property real gapLg: 20

    color: bg

    property int view: 0            // 0 generate, 1 history, 2 settings
    property bool busy: false
    property string currentJobId: ""
    property string statusMessage: ""

    function relToUrl(rel) { return "image://asset/" + rel }

    ListModel { id: results }

    Connections {
        target: Jobs
        function onJobStarted(jobId) {
            root.busy = true
            root.currentJobId = jobId
            root.statusMessage = ""
        }
        function onJobFinished(jobId, status, error, assetRelPaths) {
            root.busy = false
            root.currentJobId = ""
            if (status === "succeeded") {
                for (var i = 0; i < assetRelPaths.length; ++i)
                    results.insert(0, { url: relToUrl(assetRelPaths[i]) })
                root.statusMessage = ""
            } else {
                root.statusMessage = (status === "cancelled") ? qsTr("任务已取消") : error
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 92
            color: root.surface
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Repeater {
                    model: [qsTr("生成"), qsTr("历史"), qsTr("设置")]
                    delegate: Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 66
                        checkable: true
                        checked: root.view === index
                        onClicked: root.view = index
                        background: Rectangle {
                            radius: root.radius
                            color: parent.checked ? root.surfaceAlt : "transparent"
                            border.color: parent.checked ? root.accent : "transparent"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: modelData
                            color: parent.checked ? root.accent : root.fgMuted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 13
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.view

            // ---------------- Generate ----------------
            ScrollView {
                clip: true
                ColumnLayout {
                    width: Math.min(root.width - 140, 720)
                    x: 24
                    spacing: root.gapLg

                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("连接 · profile + 密钥"); color: root.fgMuted; font.pixelSize: 13 }
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                TextField { id: fProfile; Layout.fillWidth: true; text: "default"; placeholderText: qsTr("profile 名") }
                                TextField { id: fKey; Layout.fillWidth: true; placeholderText: qsTr("API key"); echoMode: TextInput.Password }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                TextField { id: fBase; Layout.fillWidth: true; text: "https://api.openai.com"; placeholderText: qsTr("base url") }
                                ComboBox { id: cbProto; model: ["openai", "grok", "gemini"] }
                                Button {
                                    text: qsTr("保存连接")
                                    onClicked: {
                                        if (!Profiles.addProfile(fProfile.text, fBase.text, cbProto.currentText, "", 0)) {
                                            root.statusMessage = Profiles.lastError()
                                            return
                                        }
                                        if (fKey.text.length > 0 && !Profiles.setCredential(fProfile.text, fKey.text)) {
                                            root.statusMessage = Profiles.lastError()
                                            return
                                        }
                                        root.statusMessage = qsTr("已保存 profile")
                                    }
                                }
                            }
                        }
                    }

                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("生成"); color: root.fgMuted; font.pixelSize: 13 }
                        ColumnLayout {
                            width: parent.width
                            spacing: root.gap
                            TextArea {
                                id: fPrompt
                                Layout.fillWidth: true
                                Layout.preferredHeight: 96
                                placeholderText: qsTr("描述你要生成的图像…")
                                wrapMode: TextArea.Wrap
                            }
                            GridLayout {
                                columns: 2
                                columnSpacing: root.gap
                                rowSpacing: 8
                                Layout.fillWidth: true
                                Label { text: qsTr("模型"); color: root.fgMuted }
                                TextField { id: fModel; Layout.fillWidth: true; text: "gpt-image-2" }
                                Label { text: qsTr("尺寸"); color: root.fgMuted }
                                ComboBox { id: cbSize; editable: true; model: ["1024x1024", "1536x1024", "1024x1536", "auto"] }
                                Label { text: qsTr("数量 n"); color: root.fgMuted }
                                SpinBox { id: sbN; from: 1; to: 8 }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                Button {
                                    text: root.busy ? qsTr("生成中…") : qsTr("生成")
                                    enabled: !root.busy && fPrompt.text.length > 0
                                    onClicked: {
                                        var sz = cbSize.editText ? cbSize.editText.text : cbSize.currentText
                                        var id = Jobs.generateJob(fProfile.text, fModel.text, fPrompt.text, sz, sbN.value, "")
                                        if (!id)
                                            root.statusMessage = Jobs.lastError()
                                    }
                                }
                                Button { text: qsTr("取消"); enabled: root.busy && root.currentJobId.length > 0; onClicked: Jobs.cancel(root.currentJobId) }
                                BusyIndicator { running: root.busy; visible: root.busy; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                                Item { Layout.fillWidth: true }
                            }
                            Label { text: root.statusMessage; color: root.danger; visible: root.statusMessage.length > 0; wrapMode: Text.WordWrap }
                        }
                    }

                    Label { text: qsTr("结果"); color: root.fgMuted; visible: results.count > 0; font.bold: true }
                    GridLayout {
                        columns: 3
                        columnSpacing: root.gap
                        rowSpacing: root.gap
                        Layout.fillWidth: true
                        Repeater {
                            model: results
                            delegate: Rectangle {
                                Layout.preferredWidth: 200
                                Layout.preferredHeight: 200
                                radius: root.radius
                                color: root.surface
                                border.color: root.borderColor
                                clip: true
                                Image {
                                    id: img
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    source: model.url
                                    sourceSize.width: 512
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                                BusyIndicator {
                                    anchors.centerIn: parent
                                    running: img.status === Image.Loading
                                    visible: running
                                }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: root.gapLg }
                }
            }

            // ---------------- History / Settings (GUI-E / GUI-F) ----------------
            Item {
                Label { anchors.centerIn: parent; text: qsTr("历史视图 · 待 GUI-E"); color: root.fgMuted }
            }
            Item {
                Label { anchors.centerIn: parent; text: qsTr("设置视图 · 待 GUI-F"); color: root.fgMuted }
            }
        }
    }
}
