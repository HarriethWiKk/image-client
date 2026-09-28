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
    property bool dropping: false   // true while a drag is over the reference card
    property string currentJobId: ""
    property string statusMessage: ""

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
                        cellHeight: 260
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
                                        spacing: 6
                                        Button {
                                            text: model.pinned ? qsTr("取消置顶") : qsTr("置顶")
                                            onClicked: History.pin(index, !model.pinned)
                                        }
                                        Button {
                                            text: qsTr("重试")
                                            onClicked: {
                                                if (Jobs.retryFromHistory(model.jobId) === "") {
                                                    var e = Jobs.lastError()
                                                    if (e.length > 0) root.statusMessage = e
                                                }
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                        Button { text: qsTr("删除"); onClicked: History.removeAt(index) }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item {
                Label { anchors.centerIn: parent; text: qsTr("设置视图 · 待 GUI-F"); color: root.fgMuted }
            }
        }
    }

    // GUI-D lightbox: binds to the Lightbox controller; opens when a result thumbnail is clicked.
    LightboxOverlay { }
}
