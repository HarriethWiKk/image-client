// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs

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

    // Palette switches on Settings.themeName (persisted). Names are stable so all existing
    // bindings keep working; a proper Theme singleton is a later refactor.
    readonly property bool dark: Settings.themeName !== "light"
    readonly property color bg: dark ? "#141414" : "#f4f4f6"
    readonly property color surface: dark ? "#1e1e1e" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#282828" : "#e9e9ee"
    readonly property color borderColor: dark ? "#3a3a3a" : "#d3d3da"
    readonly property color fg: dark ? "#e8e8e8" : "#1a1a1a"
    readonly property color fgMuted: dark ? "#9a9a9a" : "#66666e"
    readonly property color accent: "#4a86d8"
    readonly property color accentFg: "#ffffff"  // text drawn on an accent-filled surface
    readonly property color danger: dark ? "#e0655f" : "#c23b34"
    readonly property real radius: 10
    readonly property real gap: 12
    readonly property real gapLg: 20

    color: bg

    property int view: 0            // 0 generate, 1 history, 2 settings
    property bool busy: false
    property bool dropping: false   // true while a drag is over the reference card
    property string currentJobId: ""
    property string statusMessage: ""
    property string editingName: ""   // profile being edited in the settings editor ("" = new)

    function resetProfileEditor() {
        editingName = ""
        eName.text = ""; eName.enabled = true; eBase.text = ""; eModel.text = ""
        eTimeout.value = 300; eKey.text = ""
    }

    function relToUrl(rel) { return "image://asset/" + rel }

    // Snapshot the current results as the lightbox's navigation set.
    function collectResultUrls() {
        var urls = []
        for (var i = 0; i < results.count; ++i)
            urls.push(results.get(i).url)
        return urls
    }

    function relPathsToUrls(rels) {
        var urls = []
        for (var i = 0; i < rels.length; ++i)
            urls.push(relToUrl(rels[i]))
        return urls
    }

    function statusLabel(s) {
        if (s === "succeeded") return qsTr("成功")
        if (s === "failed") return qsTr("失败")
        if (s === "cancelled") return qsTr("已取消")
        if (s === "running") return qsTr("生成中")
        if (s === "queued") return qsTr("排队")
        return s
    }
    function statusColor(s) {
        if (s === "succeeded") return root.accent
        if (s === "failed") return root.danger
        return root.fgMuted
    }

    ListModel { id: results }

    // Stage one source (from drop or file dialog). The controller emits referencesChanged() on
    // every mutation and the Repeater binds straight to Jobs.referenceSources, so no manual
    // refresh here -- surface a rejection reason only.
    function stageReference(urlOrPath) {
        if (Jobs.addReferencePath(urlOrPath) === "") {
            var e = Jobs.lastError()
            if (e.length > 0)
                statusMessage = e
        }
    }

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
            // Keep the persisted-history view current (a finished or retried job now has a row).
            History.refresh()
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
            onCurrentIndexChanged: if (currentIndex === 1) History.refresh()

            // ---------------- Generate ----------------
            ScrollView {
                clip: true
                ColumnLayout {
                    width: Math.min(root.width - 140, 720)
                    x: 24
                    spacing: root.gapLg

                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("连接 · Provider + 密钥"); color: root.fgMuted; font.pixelSize: 13 }
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                TextField { id: fProfile; Layout.fillWidth: true; text: "default"; placeholderText: qsTr("Provider 名") }
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
                                        root.statusMessage = qsTr("已保存 Provider")
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
                                    text: root.busy ? qsTr("生成中…")
                                          : (Jobs.referenceSources.length > 0 ? qsTr("图生图") : qsTr("生成"))
                                    enabled: !root.busy && fPrompt.text.length > 0
                                    onClicked: {
                                        var sz = cbSize.editText ? cbSize.editText.text : cbSize.currentText
                                        var id = Jobs.referenceSources.length > 0
                                            ? Jobs.generateEdit(fProfile.text, fModel.text, fPrompt.text, sz, sbN.value, "")
                                            : Jobs.generateJob(fProfile.text, fModel.text, fPrompt.text, sz, sbN.value, "")
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

                    Rectangle {
                        id: refCard
                        Layout.fillWidth: true
                        radius: root.radius
                        color: root.surface
                        border.color: root.dropping ? root.accent : root.borderColor
                        border.width: root.dropping ? 2 : 1
                        implicitHeight: refCol.implicitHeight + 24

                        ColumnLayout {
                            id: refCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            spacing: root.gap

                            Text { text: qsTr("参考图 · 图生图（拖放 / 选择 / 粘贴，加入后自动切到编辑）"); color: root.fgMuted; font.pixelSize: 13 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                Button {
                                    text: qsTr("选择图片…")
                                    onClicked: fileDialog.open()
                                }
                                Button {
                                    text: qsTr("从剪贴板粘贴")
                                    onClicked: {
                                        if (!Jobs.addReferenceFromClipboard()) {
                                            var e = Jobs.lastError()
                                            if (e.length > 0)
                                                root.statusMessage = e
                                        }
                                    }
                                }
                                Button {
                                    text: qsTr("清空")
                                    visible: Jobs.referenceSources.length > 0
                                    onClicked: Jobs.clearReferences()
                                }
                                Item { Layout.fillWidth: true }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: root.gap
                                Repeater {
                                    model: Jobs.referenceSources
                                    delegate: Rectangle {
                                        width: 96
                                        height: 96
                                        radius: root.radius
                                        color: root.bg
                                        border.color: root.borderColor
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            source: modelData
                                            sourceSize.width: 256
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                        }
                                        // Click the reference body to preview it full-size in the lightbox; the ×
                                        // Button stays declared after it (on top) so removal still wins.
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: { Lightbox.model = Jobs.referenceSources; Lightbox.openAt(index) }
                                        }
                                        Button {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            width: 22
                                            height: 22
                                            text: "×"
                                            onClicked: Jobs.removeReference(modelData)
                                        }
                                    }
                                }
                            }
                        }

                        // Full-card drop overlay, declared LAST so it sits on top. A DropArea only
                        // handles drag-and-drop events, so button and thumbnail clicks still reach
                        // the items below it (verified on the real window). During a drag the whole
                        // card lights up as the accepting surface; the highlight clears on exit/drop.
                        DropArea {
                            anchors.fill: parent
                            onEntered: (drag) => { if (drag.hasUrls) root.dropping = true }
                            onPositionChanged: (drag) => { if (drag.hasUrls) root.dropping = true }
                            onExited: root.dropping = false
                            onDropped: (drop) => {
                                root.dropping = false
                                if (drop.hasUrls) {
                                    for (var i = 0; i < drop.urls.length; ++i)
                                        root.stageReference(drop.urls[i].toString())
                                    drop.acceptProposedAction()
                                }
                            }
                        }
                    }

                    FileDialog {
                        id: fileDialog
                        title: qsTr("选择参考图")
                        nameFilters: [qsTr("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp)"), qsTr("所有文件 (*)")]
                        fileMode: FileDialog.OpenFiles
                        onAccepted: {
                            // Qt 6.8's QtQuick.Dialogs FileDialog exposes selectedFiles (a list of
                            // urls) for multi-select -- NOT selectedUrls, which does not exist on this
                            // type (its absence threw "ReferenceError" and silently dropped the picks).
                            for (var i = 0; i < fileDialog.selectedFiles.length; ++i)
                                root.stageReference(fileDialog.selectedFiles[i].toString())
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
                                border.color: hover.containsMouse ? root.accent : root.borderColor
                                border.width: hover.containsMouse ? 2 : 1
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
                                MouseArea {
                                    id: hover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { Lightbox.model = root.collectResultUrls(); Lightbox.openAt(index) }
                                }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: root.gapLg }
                }
            }

            // ---------------- History (GUI-E) ----------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    Label { text: qsTr("历史 · 跨重启保留（sqlite）"); color: root.fgMuted; font.bold: true }
                    Label {
                        visible: History.count === 0
                        text: qsTr("还没有历史 · 去「生成」页出图")
                        color: root.fgMuted
                    }

                    GridView {
                        id: histGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        cellWidth: 224
                        cellHeight: 320
                        boundsBehavior: Flickable.StopAtBounds
                        model: History
                        ScrollIndicator.vertical: ScrollIndicator { }
                        onAtYEndChanged: if (atYEnd && History.hasMore) History.loadMore()

                        delegate: Item {
                            width: histGrid.cellWidth
                            height: histGrid.cellHeight
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 6
                                radius: root.radius
                                color: root.surface
                                border.color: model.pinned ? root.accent : root.borderColor
                                border.width: model.pinned ? 2 : 1
                                clip: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 150
                                        radius: 6
                                        color: root.bg
                                        border.color: root.borderColor
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            source: model.thumbnail ? (root.relToUrl(model.thumbnail)) : ""
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: !model.thumbnail
                                            text: root.statusLabel(model.status || "")
                                            color: root.statusColor(model.status || "")
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !!model.thumbnail
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                Lightbox.model = root.relPathsToUrls(History.resultRelPaths(index))
                                                Lightbox.openAt(0)
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Text { text: root.statusLabel(model.status || ""); color: root.statusColor(model.status || ""); font.pixelSize: 12 }
                                        Text { text: model.mode || ""; color: root.fgMuted; font.pixelSize: 11 }
                                        Item { Layout.fillWidth: true }
                                        Text { visible: !!model.pinned; text: "★"; color: root.accent; font.pixelSize: 12 }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: model.prompt || ""
                                        color: root.fg
                                        elide: Text.ElideRight
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        visible: model.status === "failed" && !!model.error && model.error.length > 0
                                        text: model.error || ""
                                        color: root.danger
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        font.pixelSize: 11
                                    }
                                    Item { Layout.fillHeight: true }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        ToolButton {
                                            text: model.pinned ? "★" : "☆"
                                            hoverEnabled: true
                                            ToolTip.text: model.pinned ? qsTr("取消置顶") : qsTr("置顶")
                                            ToolTip.visible: hovered
                                            onClicked: History.pin(index, !model.pinned)
                                        }
                                        ToolButton {
                                            text: "⟳"
                                            hoverEnabled: true
                                            ToolTip.text: qsTr("重试（图生图会还原参考图）")
                                            ToolTip.visible: hovered
                                            onClicked: {
                                                if (Jobs.retryFromHistory(model.jobId || "") === "") {
                                                    var e = Jobs.lastError()
                                                    if (e.length > 0) root.statusMessage = e
                                                }
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                        ToolButton {
                                            text: "✕"
                                            hoverEnabled: true
                                            ToolTip.text: qsTr("删除")
                                            ToolTip.visible: hovered
                                            onClicked: History.removeAt(index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // ---------------- Settings (GUI-F) ----------------
            ScrollView {
                clip: true
                ColumnLayout {
                    width: Math.min(root.width - 140, 760)
                    x: 24
                    spacing: root.gapLg

                    // ---- Profiles ----
                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("模型供应商 Provider（多服务商快速切换）"); color: root.fgMuted; font.pixelSize: 13 }
                        ColumnLayout {
                            width: parent.width
                            spacing: 8

                            Text { text: qsTr("已有 Provider"); color: root.fgMuted; font.pixelSize: 12; visible: Profiles.profiles.length > 0 }
                            Repeater {
                                model: Profiles.profiles
                                delegate: RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    property var p: modelData
                                    Text {
                                        text: p.name + " · " + p.protocol + (p.hasCredential ? qsTr(" · 有密钥") : qsTr(" · 无密钥"))
                                        color: root.fg; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Button {
                                        text: qsTr("编辑")
                                        onClicked: {
                                            root.editingName = p.name
                                            eName.text = p.name; eName.enabled = false
                                            eBase.text = p.baseUrl; eProto.currentText = p.protocol
                                            eModel.text = p.imageModel; eTimeout.value = p.timeoutSeconds; eKey.text = ""
                                        }
                                    }
                                    Button { text: qsTr("设密钥"); onClicked: { root.editingName = p.name; eName.text = p.name; eName.enabled = false; eKey.focus = true } }
                                    Button { text: qsTr("删除密钥"); visible: p.hasCredential; onClicked: Profiles.deleteCredential(p.name) }
                                    Button {
                                        text: qsTr("删除")
                                        onClicked: { if (!Profiles.removeProfileByName(p.name)) root.statusMessage = Profiles.lastError() }
                                    }
                                }
                            }

                            // Editor (add or update-by-name).
                            GridLayout {
                                columns: 2
                                columnSpacing: root.gap
                                rowSpacing: 6
                                Layout.fillWidth: true
                                Label { text: qsTr("名称"); color: root.fgMuted }
                                TextField { id: eName; Layout.fillWidth: true; placeholderText: qsTr("Provider 名") }
                                Label { text: qsTr("Base URL"); color: root.fgMuted }
                                TextField { id: eBase; Layout.fillWidth: true; placeholderText: "https://api.openai.com" }
                                Label { text: qsTr("协议"); color: root.fgMuted }
                                ComboBox { id: eProto; model: Profiles.protocols() }
                                Label { text: qsTr("默认模型"); color: root.fgMuted }
                                TextField { id: eModel; Layout.fillWidth: true }
                                Label { text: qsTr("超时(秒)"); color: root.fgMuted }
                                SpinBox { id: eTimeout; from: 0; to: 1800; value: 300; editable: true }
                                Label { text: qsTr("API 密钥"); color: root.fgMuted }
                                TextField { id: eKey; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("留空则不改") }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                Button {
                                    text: root.editingName.length > 0 ? qsTr("保存修改") : qsTr("新增 Provider")
                                    enabled: eName.text.length > 0 && eBase.text.length > 0
                                    onClicked: {
                                        if (!Profiles.addProfile(eName.text, eBase.text, eProto.currentText, eModel.text, eTimeout.value)) {
                                            root.statusMessage = Profiles.lastError(); return
                                        }
                                        if (eKey.text.length > 0 && !Profiles.setCredential(eName.text, eKey.text)) {
                                            root.statusMessage = Profiles.lastError(); return
                                        }
                                        root.resetProfileEditor()
                                    }
                                }
                                Button { text: qsTr("取消"); visible: root.editingName.length > 0; onClicked: root.resetProfileEditor() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    // ---- Retention ----
                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("历史保留与存储"); color: root.fgMuted; font.pixelSize: 13 }
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            GridLayout {
                                columns: 2
                                columnSpacing: root.gap
                                rowSpacing: 6
                                Layout.fillWidth: true
                                Label { text: qsTr("最多保留任务数"); color: root.fgMuted }
                                SpinBox {
                                    id: sbRows; from: 1; to: 100000; editable: true
                                    value: Settings.retentionRows
                                    onValueModified: Settings.retentionRows = value
                                }
                                Label { text: qsTr("存储上限 (MiB)"); color: root.fgMuted }
                                SpinBox {
                                    id: sbMiB; from: 64; to: 1024 * 1024; editable: true
                                    value: Math.round(Settings.retentionBytes / (1024 * 1024))
                                    onValueModified: Settings.retentionBytes = value * 1024 * 1024
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                Text { text: qsTr("已用:") + " " + (Settings.usedBytes / (1024 * 1024)).toFixed(1) + " MiB"; color: root.fgMuted }
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: qsTr("立即清理")
                                    onClicked: { var n = Settings.applyRetention(); root.statusMessage = qsTr("已清理 %1 个任务").arg(n) }
                                }
                            }
                            Text { text: qsTr("置顶(pinned)的任务不计入预算、不会被清理。"); color: root.fgMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        }
                    }

                    // ---- Trusted hosts ----
                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("可信主机（SSRF 例外 · 重启生效）"); color: root.fgMuted; font.pixelSize: 13 }
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: Settings.trustedHosts
                                delegate: RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: modelData; color: root.fg; Layout.fillWidth: true }
                                    Button { text: "✕"; onClicked: Settings.removeTrustedHost(modelData) }
                                }
                            }
                            Text { visible: Settings.trustedHosts.length === 0; text: qsTr("（无）"); color: root.fgMuted }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.gap
                                TextField { id: eHost; Layout.fillWidth: true; placeholderText: qsTr("仅主机名，如 gw.internal") }
                                Button { text: qsTr("添加"); enabled: eHost.text.trim().length > 0; onClicked: { Settings.addTrustedHost(eHost.text); eHost.text = "" } }
                            }
                        }
                    }

                    // ---- Theme + WebP ----
                    GroupBox {
                        Layout.fillWidth: true
                        label: Text { text: qsTr("外观与格式"); color: root.fgMuted; font.pixelSize: 13 }
                        RowLayout {
                            width: parent.width
                            spacing: root.gap
                            Label { text: qsTr("浅色主题"); color: root.fgMuted }
                            Switch {
                                checked: Settings.themeName === "light"
                                onToggled: Settings.themeName = checked ? "light" : "dark"
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("本机 Qt 无 WebP 解码插件：webp 结果会落盘但无法在应用内预览。")
                                color: root.fgMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.maximumWidth: 360
                            }
                        }
                    }
                    Item { Layout.preferredHeight: root.gapLg }
                }
            }
        }
    }

    // GUI-D lightbox: binds to the Lightbox controller; opens when a result thumbnail is clicked.
    LightboxOverlay { }
}
