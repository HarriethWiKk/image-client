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

    // Palette bridged directly to Theme singleton so all existing bindings keep working.
    readonly property bool dark: Theme.dark
    readonly property color bg: Theme.bg
    readonly property color surface: Theme.surface
    readonly property color surfaceAlt: Theme.surfaceAlt
    readonly property color borderColor: Theme.borderColor
    readonly property color fg: Theme.fg
    readonly property color fgMuted: Theme.fgMuted
    readonly property color accent: Theme.accent
    readonly property color accentFg: Theme.accentFg
    readonly property color danger: Theme.danger
    readonly property real radius: Theme.radius
    readonly property real gap: Theme.gap
    readonly property real gapLg: Theme.gapLg

    color: bg

    property int view: 0            // 0 generate, 1 history, 2 settings
    property bool busy: false
    property bool dropping: false   // true while a drag is over the reference card
    property string currentJobId: ""
    property string statusMessage: ""
    property string editingName: ""   // profile being edited in the settings editor ("" = new)
    property bool providerDrawerOpen: false

    function showToast(msg, type) {
        if (toast) toast.show(msg, type || "info")
    }

    onStatusMessageChanged: {
        if (statusMessage.length > 0)
            showToast(statusMessage, "error")
    }

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

    // Editable combos keep typed text in editText; currentText only follows a selected item and
    // stays empty for a value that is not in the model. Every read of a user-editable combo goes
    // through this helper.
    function comboText(combo) {
        return combo.editText.length > 0 ? combo.editText : combo.currentText
    }

    // Actions shared by the buttons and the keyboard shortcuts (SPEC 9.1: 生成 / 取消 / 切模型).
    function doGenerate() {
        if (busy || fPrompt.text.length === 0)
            return
        var provider = root.comboText(cbProvider)
        var model = root.comboText(cbModel)
        var sz = root.comboText(cbSize)
        var id = Jobs.referenceSources.length > 0
            ? Jobs.generateEdit(provider, model, fPrompt.text, sz, sbN.value, "")
            : Jobs.generateJob(provider, model, fPrompt.text, sz, sbN.value, "")
        if (!id)
            statusMessage = Jobs.lastError()
    }
    function doCancel() {
        if (busy && currentJobId.length > 0)
            Jobs.cancel(currentJobId)
    }
    // Select a non-editable ComboBox entry by its model value. `currentText` is read-only on a
    // non-editable ComboBox, so setting a selection by value must go through currentIndex.
    function setCombo(combo, value) {
        for (var i = 0; i < combo.count; ++i) {
            if (combo.model[i] === value) {
                combo.currentIndex = i
                return
            }
        }
    }

    // Selecting a provider loads its base/protocol into the editor and (re)fetches its model list.
    // The list is provider-scoped, so a same-named model under two providers stays distinct --
    // generation submits the (provider, model) pair.
    function selectProvider(name) {
        var d = Profiles.profileDetail(name)
        if (d && d.name === name) {
            fBase.text = d.baseUrl
            root.setCombo(cbProto, d.protocol)
            if (d.imageModel && d.imageModel.length > 0)
                cbModel.editText = d.imageModel
        }
        Models.refresh(name)
    }
    // 切模型: step the model picker within the current provider's list.
    function cycleModel() {
        if (cbModel.count > 0)
            cbModel.currentIndex = (cbModel.currentIndex + 1) % cbModel.count
    }
    // 切 Provider: step to the next configured provider (and load its models).
    function cycleProvider() {
        if (cbProvider.count > 0) {
            cbProvider.currentIndex = (cbProvider.currentIndex + 1) % cbProvider.count
            selectProvider(root.comboText(cbProvider))
        } else {
            statusMessage = qsTr("还没有可切换的 Provider")
        }
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
            return false
        }
        return true
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

        // Modern Left Navigation Rail
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 76
            color: Theme.surface
            border.color: Theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 16
                anchors.bottomMargin: 16
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: Theme.gapSm

                // App Brand Icon
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: Theme.radius
                    color: Theme.accentSubtle
                    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "⚡"
                        font.pixelSize: Theme.fontSizeLg
                    }
                }

                Item { Layout.preferredHeight: 12 }

                Repeater {
                    model: [
                        { name: qsTr("生成"), icon: "🎨" },
                        { name: qsTr("历史"), icon: "📜" },
                        { name: qsTr("设置"), icon: "⚙️" }
                    ]
                    delegate: Rectangle {
                        id: navBtn
                        Layout.fillWidth: true
                        Layout.preferredHeight: 56
                        radius: Theme.radiusSm
                        color: (root.view === index) ? Theme.surfaceAlt : (navMouse.containsMouse ? Theme.surfaceHover : "transparent")

                        // Active Indicator Strip on the left
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: 24
                            radius: 1.5
                            color: Theme.accent
                            visible: root.view === index
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.gapXs
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.icon
                                font.pixelSize: Theme.fontSize
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.name
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: (root.view === index) ? Font.DemiBold : Font.Normal
                                color: (root.view === index) ? Theme.accent : (navMouse.containsMouse ? Theme.fg : Theme.fgMuted)
                            }
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.view = index
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

            // ---------------- Generate (双栏现代工作台) ----------------
            Item {
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.gapXl
                    spacing: Theme.gapLg

                    // 左侧控制台面板 (稳定 360px 宽度，各卡片自适应内部内容高度)
                    ScrollView {
                        Layout.preferredWidth: 360
                        Layout.minimumWidth: 320
                        Layout.maximumWidth: 420
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded

                        ColumnLayout {
                            width: parent ? (parent.width - Theme.scrollBarWidth) : 348
                            spacing: 14

                            // 1. 服务商与连接卡片 (可收起抽屉)
                            AppCard {
                                Layout.fillWidth: true
                                implicitHeight: provCol.implicitHeight + Theme.gap * 2

                                ColumnLayout {
                                    id: provCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.gap
                                    spacing: Theme.gapSm

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm

                                        Text {
                                            text: "🌐"
                                            font.pixelSize: Theme.fontSizeSm
                                        }

                                        Text {
                                            text: {
                                                var name = root.comboText(cbProvider)
                                                return name.length > 0 ? name : qsTr("选择 Provider")
                                            }
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSize
                                            font.bold: true
                                            color: Theme.fg
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }

                                        AppStatusBadge {
                                            text: cbProto.currentText
                                            status: "running"
                                        }

                                        AppButton {
                                            variant: "ghost"
                                            implicitHeight: 28
                                            text: root.providerDrawerOpen ? qsTr("收起 ▴") : qsTr("配置 ▾")
                                            onClicked: root.providerDrawerOpen = !root.providerDrawerOpen
                                        }
                                    }

                                    // 可折叠配置抽屉
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm
                                        visible: root.providerDrawerOpen

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: Theme.borderColor
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.gapSm
                                            AppComboBox {
                                                id: cbProvider
                                                Layout.fillWidth: true
                                                editable: true
                                                model: Profiles.profiles
                                                textRole: "name"
                                                onActivated: root.selectProvider(currentText)
                                                Component.onCompleted: {
                                                    // Prefer "default" when it exists, otherwise the first
                                                    // configured provider. Leaving the picker empty made every
                                                    // other setup open on "未找到 Provider「」".
                                                    var idx = -1
                                                    for (var i = 0; i < model.length; ++i)
                                                        if (model[i].name === "default") { idx = i; break }
                                                    if (idx < 0 && model.length > 0)
                                                        idx = 0
                                                    if (idx >= 0) {
                                                        currentIndex = idx
                                                        root.selectProvider(currentText)
                                                    }
                                                }
                                            }
                                            AppTextField {
                                                id: fKey
                                                Layout.fillWidth: true
                                                placeholderText: qsTr("API key")
                                                echoMode: TextInput.Password
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.gapSm
                                            AppTextField {
                                                id: fBase
                                                Layout.fillWidth: true
                                                text: "https://api.openai.com"
                                                placeholderText: qsTr("base url")
                                            }
                                            AppComboBox {
                                                id: cbProto
                                                Layout.preferredWidth: 100
                                                model: ["openai", "grok", "gemini"]
                                            }
                                            AppButton {
                                                variant: "primary"
                                                text: qsTr("保存")
                                                onClicked: {
                                                    if (!Profiles.addProfile(root.comboText(cbProvider), fBase.text, cbProto.currentText, root.comboText(cbModel), 0)) {
                                                        root.showToast(Profiles.lastError(), "error")
                                                        return
                                                    }
                                                    if (fKey.text.length > 0 && !Profiles.setCredential(root.comboText(cbProvider), fKey.text)) {
                                                        root.showToast(Profiles.lastError(), "error")
                                                        return
                                                    }
                                                    root.showToast(qsTr("已成功保存 Provider 配置"), "success")
                                                    Models.refresh(root.comboText(cbProvider))
                                                    root.providerDrawerOpen = false
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // 2. 提示词卡片 (Prompt)
                            AppCard {
                                Layout.fillWidth: true
                                implicitHeight: promptCol.implicitHeight + Theme.gap * 2

                                ColumnLayout {
                                    id: promptCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.gap
                                    spacing: Theme.gapSm

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: qsTr("提示词 (Prompt)")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSize
                                            font.weight: Font.DemiBold
                                            color: Theme.fg
                                            Layout.fillWidth: true
                                        }
                                        AppIconButton {
                                            text: "✕"
                                            toolTipText: qsTr("清空提示词")
                                            visible: fPrompt.text.length > 0
                                            onClicked: fPrompt.text = ""
                                        }
                                    }

                                    AppTextArea {
                                        id: fPrompt
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 120
                                        placeholderText: qsTr("描述你要生成的图像画面…")
                                        hintText: "Ctrl+Enter 快捷提交"
                                        maxChars: 16000
                                    }
                                }
                            }

                            // 3. 模型与参数设置卡片
                            AppCard {
                                Layout.fillWidth: true
                                implicitHeight: paramCol.implicitHeight + Theme.gap * 2

                                ColumnLayout {
                                    id: paramCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.gap
                                    spacing: 10

                                    // 模型选择行
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm / 2

                                        Label {
                                            text: qsTr("模型")
                                            color: Theme.fgMuted
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeSm
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.gapSm

                                            AppComboBox {
                                                id: cbModel
                                                Layout.fillWidth: true
                                                editable: true
                                                model: Models.models
                                                Component.onCompleted: if (editText.length === 0) editText = "gpt-image-2"
                                            }

                                            BusyIndicator {
                                                running: Models.loading
                                                visible: Models.loading
                                                Layout.preferredWidth: 20
                                                Layout.preferredHeight: 20
                                            }

                                            AppIconButton {
                                                text: "⟳"
                                                toolTipText: qsTr("重新拉取该 Provider 的模型列表 (Ctrl+M 切模型)")
                                                onClicked: Models.refresh(root.comboText(cbProvider))
                                            }
                                        }

                                        Text {
                                            visible: Models.error.length > 0
                                            text: Models.error
                                            color: Theme.danger
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeXs
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                        }
                                    }

                                    // 尺寸与张数行
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gap

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.gapSm / 2
                                            Label {
                                                text: qsTr("尺寸")
                                                color: Theme.fgMuted
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeSm
                                            }
                                            AppComboBox {
                                                id: cbSize
                                                Layout.fillWidth: true
                                                editable: true
                                                model: ["1024x1024", "1536x1024", "1024x1536", "auto"]
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: 96
                                            spacing: Theme.gapSm / 2
                                            Label {
                                                text: qsTr("数量 (n)")
                                                color: Theme.fgMuted
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeSm
                                            }
                                            SpinBox {
                                                id: sbN
                                                from: 1
                                                to: 8
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                }
                            }

                            // 4. 参考图卡片 (图生图)
                            AppCard {
                                id: refCard
                                Layout.fillWidth: true
                                border.color: root.dropping ? Theme.accent : Theme.borderColor
                                border.width: root.dropping ? 2 : 1
                                implicitHeight: refCol.implicitHeight + Theme.gap * 2

                                ColumnLayout {
                                    id: refCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.gap
                                    spacing: Theme.gapSm

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: qsTr("参考图 (图生图)")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSize
                                            font.weight: Font.DemiBold
                                            color: Theme.fg
                                            Layout.fillWidth: true
                                        }
                                        AppStatusBadge {
                                            visible: Jobs.referenceSources.length > 0
                                            text: qsTr("%1 张参考图").arg(Jobs.referenceSources.length)
                                            status: "running"
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm

                                        AppButton {
                                            text: qsTr("选择图片…")
                                            onClicked: fileDialog.open()
                                        }
                                        AppButton {
                                            text: qsTr("剪贴板粘贴")
                                            onClicked: {
                                                if (!Jobs.addReferenceFromClipboard()) {
                                                    var e = Jobs.lastError()
                                                    if (e.length > 0)
                                                        root.showToast(e, "error")
                                                }
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                        AppIconButton {
                                            text: "🗑"
                                            toolTipText: qsTr("清空所有参考图")
                                            visible: Jobs.referenceSources.length > 0
                                            onClicked: Jobs.clearReferences()
                                        }
                                    }

                                    // Empty dashed placeholder
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 64
                                        visible: Jobs.referenceSources.length === 0
                                        radius: Theme.radiusSm
                                        color: root.dropping ? Theme.accentSubtle : Theme.surfaceAlt
                                        border.color: root.dropping ? Theme.accent : Theme.borderLight
                                        border.width: 1

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: Theme.gapXs
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: "📥 " + qsTr("拖放图片至此，或直接 Ctrl+V 粘贴")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeSm
                                                color: root.dropping ? Theme.accent : Theme.fgMuted
                                            }
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: qsTr("支持 PNG, JPEG, WebP, BMP (最多 16 张)")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeXs
                                                color: Theme.fgSubtle
                                            }
                                        }
                                    }

                                    // Staged thumbnail flow
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm
                                        visible: Jobs.referenceSources.length > 0

                                        Repeater {
                                            model: Jobs.referenceSources
                                            delegate: Rectangle {
                                                width: 80
                                                height: 80
                                                radius: Theme.radiusSm
                                                color: Theme.bg
                                                border.color: Theme.borderColor
                                                border.width: 1
                                                clip: true

                                                Image {
                                                    anchors.fill: parent
                                                    anchors.margins: Theme.gapXs
                                                    source: modelData
                                                    sourceSize.width: 256
                                                    fillMode: Image.PreserveAspectFit
                                                    asynchronous: true
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: { Lightbox.model = Jobs.referenceSources; Lightbox.openAt(index) }
                                                }

                                                AppIconButton {
                                                    anchors.top: parent.top
                                                    anchors.right: parent.right
                                                    anchors.margins: Theme.gapXs
                                                    text: "×"
                                                    toolTipText: qsTr("移除")
                                                    onClicked: Jobs.removeReference(modelData)
                                                }
                                            }
                                        }
                                    }
                                }

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

                            // 5. 底部主控出图栏
                            AppCard {
                                Layout.fillWidth: true
                                implicitHeight: actionRow.implicitHeight + Theme.gapLg

                                RowLayout {
                                    id: actionRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 10
                                    spacing: Theme.gap

                                    AppButton {
                                        id: btnGenerate
                                        variant: "primary"
                                        Layout.fillWidth: true
                                        implicitHeight: 40
                                        text: root.busy ? qsTr("正在生成中…")
                                              : (Jobs.referenceSources.length > 0 ? qsTr("🚀 立即图生图") : qsTr("🚀 立即生成"))
                                        enabled: !root.busy && fPrompt.text.length > 0
                                        onClicked: root.doGenerate()
                                    }

                                    AppButton {
                                        variant: "danger"
                                        implicitHeight: 40
                                        text: qsTr("取消")
                                        visible: root.busy && root.currentJobId.length > 0
                                        onClicked: root.doCancel()
                                    }

                                    BusyIndicator {
                                        running: root.busy
                                        visible: root.busy
                                        Layout.preferredWidth: 26
                                        Layout.preferredHeight: 26
                                    }
                                }
                            }

                            Item { Layout.preferredHeight: 8 }
                        }
                    }

                    // 右侧自适应画廊与画布区域 (弹性拉伸)
                    Item {
                        id: galleryArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Theme.gap

                            // 画廊顶部信息条
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapSm

                                Text {
                                    text: qsTr("生成画廊")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeLg
                                    font.bold: true
                                    color: Theme.fg
                                }

                                Text {
                                    visible: results.count > 0
                                    text: qsTr("· %1 张图像").arg(results.count)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                    color: Theme.fgMuted
                                }

                                Item { Layout.fillWidth: true }

                                AppIconButton {
                                    text: "🗑"
                                    toolTipText: qsTr("清空当前生成结果画廊")
                                    visible: results.count > 0
                                    onClicked: results.clear()
                                }
                            }

                            // 画廊滚动视图
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                ColumnLayout {
                                    width: parent ? (parent.width - Theme.scrollBarWidth) : 500
                                    spacing: Theme.gap

                                    // 生成中微光骨架卡片 (Skeleton Card)
                                    AppCard {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 180
                                        visible: root.busy

                                        SequentialAnimation on opacity {
                                            running: root.busy
                                            loops: Animation.Infinite
                                            NumberAnimation { from: 1.0; to: 0.35; duration: 750; easing.type: Easing.InOutQuad }
                                            NumberAnimation { from: 0.35; to: 1.0; duration: 750; easing.type: Easing.InOutQuad }
                                        }

                                        ColumnLayout {
                                            anchors.centerIn: parent
                                            spacing: Theme.gap

                                            BusyIndicator {
                                                running: root.busy
                                                Layout.alignment: Qt.AlignHCenter
                                                Layout.preferredWidth: 36
                                                Layout.preferredHeight: 36
                                            }

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: qsTr("AI 正在精心绘制图像，请稍候…")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSize
                                                font.weight: Font.Medium
                                                color: Theme.fg
                                            }

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: qsTr("任务已派发至模型服务端")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeXs
                                                color: Theme.fgMuted
                                            }
                                        }
                                    }

                                    // 空状态展示 (Empty State)
                                    AppCard {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 280
                                        visible: results.count === 0 && !root.busy

                                        ColumnLayout {
                                            anchors.centerIn: parent
                                            spacing: Theme.gap

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: "🎨"
                                                font.pixelSize: Theme.fontSizeHero
                                            }

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: qsTr("尚未生成任何图片")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeLg
                                                font.bold: true
                                                color: Theme.fg
                                            }

                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: qsTr("在左侧输入生动的画面描述，或拖入参考图开启图生图创作")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeSm
                                                color: Theme.fgMuted
                                            }
                                        }
                                    }

                                    // 自适应网格画廊
                                    GridLayout {
                                        id: galleryGrid
                                        Layout.fillWidth: true
                                        visible: results.count > 0
                                        readonly property real availWidth: Math.max(240, (parent ? parent.width : galleryArea.width) - 16)
                                        columns: Math.max(1, Math.floor(availWidth / Theme.cardMinWidth))
                                        columnSpacing: Theme.gap
                                        rowSpacing: Theme.gap

                                        Repeater {
                                            model: results
                                            delegate: AppCard {
                                                readonly property real itemWidth: Math.floor((galleryGrid.availWidth - (galleryGrid.columns - 1) * Theme.gap) / galleryGrid.columns)
                                                Layout.preferredWidth: Math.max(160, itemWidth)
                                                Layout.preferredHeight: Layout.preferredWidth
                                                clip: true
                                                hoverable: true

                                                Image {
                                                    id: img
                                                    anchors.fill: parent
                                                    anchors.margins: Theme.gapSm / 2
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

                                                // 悬停操作浮层
                                                Rectangle {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    height: 38
                                                    color: Theme.dark ? "#e01a1a1e" : "#f0ffffff"
                                                    visible: itemHover.containsMouse
                                                    border.color: Theme.borderColor
                                                    border.width: 1

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 8
                                                        anchors.rightMargin: 8
                                                        spacing: Theme.gapSm

                                                        AppIconButton {
                                                            text: "🔍"
                                                            toolTipText: qsTr("放大全屏查看 [Click]")
                                                            onClicked: { Lightbox.model = root.collectResultUrls(); Lightbox.openAt(index) }
                                                        }

                                                        AppIconButton {
                                                            text: "↺"
                                                            toolTipText: qsTr("复用为图生图参考图")
                                                            onClicked: {
                                                                if (root.stageReference(model.url))
                                                                    root.showToast(qsTr("已添加至参考图队列"), "success")
                                                            }
                                                        }

                                                        Item { Layout.fillWidth: true }

                                                        AppIconButton {
                                                            text: "⤓"
                                                            toolTipText: qsTr("另存为…")
                                                            onClicked: {
                                                                Lightbox.model = root.collectResultUrls()
                                                                Lightbox.openAt(index)
                                                            }
                                                        }
                                                    }
                                                }

                                                MouseArea {
                                                    id: itemHover
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: { Lightbox.model = root.collectResultUrls(); Lightbox.openAt(index) }
                                                }
                                            }
                                        }
                                    }

                                    Item { Layout.preferredHeight: Theme.gapLg }
                                }
                            }
                        }
                    }
                }
            }

            // ---------------- History (GUI-E) ----------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.gapXl
                    spacing: Theme.gap

                    // 历史视图顶部标题栏
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapSm

                        Text {
                            text: qsTr("历史画廊")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeLg
                            font.bold: true
                            color: Theme.fg
                        }

                        Text {
                            text: qsTr("（已加载 %1 条记录 · SQLite 持久存储）").arg(History.count)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            color: Theme.fgMuted
                        }

                        Item { Layout.fillWidth: true }

                        AppIconButton {
                            text: "⟳"
                            toolTipText: qsTr("重新刷新历史记录")
                            onClicked: {
                                History.refresh()
                                root.showToast(qsTr("历史记录已刷新"), "info")
                            }
                        }
                    }

                    // 空状态卡片
                    AppCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 300
                        visible: History.count === 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Theme.gap

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "📜"
                                font.pixelSize: Theme.fontSizeHero
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("暂无任何历史任务")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeLg
                                font.bold: true
                                color: Theme.fg
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("所有出图记录均会安全保存在本地 SQLite 数据库中")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                color: Theme.fgMuted
                            }

                            Item { Layout.preferredHeight: 4 }

                            AppButton {
                                Layout.alignment: Qt.AlignHCenter
                                variant: "primary"
                                text: qsTr("前往「生成」页开始创作")
                                onClicked: root.view = 0
                            }
                        }
                    }

                    // 历史网格列表
                    GridView {
                        id: histGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: History.count > 0
                        clip: true
                        cellWidth: 236
                        cellHeight: 310
                        boundsBehavior: Flickable.StopAtBounds
                        model: History
                        ScrollIndicator.vertical: ScrollIndicator { }
                        onAtYEndChanged: if (atYEnd && History.hasMore) History.loadMore()

                        delegate: Item {
                            width: histGrid.cellWidth
                            height: histGrid.cellHeight

                            AppCard {
                                anchors.fill: parent
                                anchors.margins: Theme.gapSm
                                clip: true
                                hoverable: true
                                border.color: model.pinned ? Theme.warning : (hovered ? Theme.accent : Theme.borderColor)
                                border.width: model.pinned ? 2 : 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Theme.gapSm
                                    spacing: Theme.gapSm

                                    // 缩略图容器
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 140
                                        radius: Theme.radiusSm
                                        color: Theme.bg
                                        border.color: Theme.borderColor
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: Theme.gapXs
                                            source: model.thumbnail ? root.relToUrl(model.thumbnail) : ""
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            visible: !model.thumbnail
                                            text: StatusMap.label(model.status || "")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSize
                                            color: StatusMap.color(model.status || "")
                                        }

                                        // 置顶标记徽章 (Pinned Badge)
                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: Theme.gapSm / 2
                                            width: 22
                                            height: 22
                                            radius: 11
                                            color: Theme.warning
                                            visible: !!model.pinned

                                            Text {
                                                anchors.centerIn: parent
                                                text: "★"
                                                color: "#ffffff"
                                                font.pixelSize: Theme.fontSizeXs
                                            }
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

                                    // 状态与模式标签行
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapSm

                                        AppStatusBadge {
                                            status: model.status || ""
                                        }

                                        Text {
                                            text: model.mode || ""
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeXs
                                            color: Theme.fgMuted
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            visible: !!model.durationMs
                                            text: (model.durationMs / 1000).toFixed(1) + "s"
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeXs
                                            color: Theme.fgSubtle
                                        }
                                    }

                                    // 提示词预览
                                    Text {
                                        Layout.fillWidth: true
                                        text: model.prompt || ""
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSm
                                        color: Theme.fg
                                        elide: Text.ElideRight
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                    }

                                    // 错误信息显示
                                    Text {
                                        visible: model.status === "failed" && !!model.error && model.error.length > 0
                                        text: model.error || ""
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                        color: Theme.danger
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Item { Layout.fillHeight: true }

                                    // 底部操作栏
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gapXs

                                        AppIconButton {
                                            text: model.pinned ? "★" : "☆"
                                            toolTipText: model.pinned ? qsTr("取消置顶") : qsTr("置顶此任务 (不受自动清理影响)")
                                            customTextColor: model.pinned ? Theme.warning : Theme.fgMuted
                                            onClicked: History.pin(index, !model.pinned)
                                        }

                                        AppIconButton {
                                            text: "⟳"
                                            toolTipText: qsTr("以此参数重试出图 (图生图会自动还原参考图)")
                                            onClicked: {
                                                if (Jobs.retryFromHistory(model.jobId || "") === "") {
                                                    var e = Jobs.lastError()
                                                    if (e.length > 0) root.showToast(e, "error")
                                                } else {
                                                    root.showToast(qsTr("已重新提交出图任务"), "success")
                                                    root.view = 0
                                                }
                                            }
                                        }

                                        AppIconButton {
                                            text: "↺"
                                            toolTipText: qsTr("将该出图设为新的图生图参考图")
                                            visible: !!model.thumbnail
                                            onClicked: {
                                                if (root.stageReference(root.relToUrl(model.thumbnail))) {
                                                    root.showToast(qsTr("已加入图生图参考图"), "success")
                                                    root.view = 0
                                                }
                                            }
                                        }

                                        Item { Layout.fillWidth: true }

                                        AppIconButton {
                                            text: "✕"
                                            toolTipText: qsTr("软删除该条记录")
                                            onClicked: {
                                                // Toast first: removeAt() drops the row and tears down this
                                                // delegate's context, so a later root.* lookup throws
                                                // "ReferenceError: root is not defined".
                                                root.showToast(qsTr("已移除该历史记录"), "info")
                                                History.removeAt(index)
                                            }
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
                id: settingsScroll
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: settingsCol
                    width: Math.min(Math.max(400, settingsScroll.width - 48), 800)
                    x: Math.max(16, (settingsScroll.width - width) / 2)
                    spacing: Theme.gapLg

                    Item { Layout.preferredHeight: Theme.gap }

                    // ---- 1. Providers ----
                    AppCard {
                        Layout.fillWidth: true
                        implicitHeight: setProvCol.implicitHeight + Theme.gapXl * 2

                        ColumnLayout {
                            id: setProvCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            spacing: Theme.gap

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "🌐 " + qsTr("模型服务商配置 (Providers)")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeLg
                                    font.bold: true
                                    color: Theme.fg
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("已配置 %1 个服务商").arg(Profiles.profiles.length)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                    color: Theme.fgMuted
                                }
                            }

                            // 已配置服务商列表
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapSm
                                visible: Profiles.profiles.length > 0

                                Repeater {
                                    model: Profiles.profiles
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 46
                                        radius: Theme.radiusSm
                                        color: Theme.surfaceAlt
                                        border.color: Theme.borderColor
                                        border.width: 1
                                        property var p: modelData

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 10
                                            spacing: Theme.gapSm

                                            Text {
                                                text: p.name
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSize
                                                font.bold: true
                                                color: Theme.fg
                                            }

                                            AppStatusBadge {
                                                text: p.protocol
                                                status: "running"
                                            }

                                            Text {
                                                text: p.hasCredential ? "🔑 " + qsTr("已存密钥") : "⚠️ " + qsTr("无密钥")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeXs
                                                color: p.hasCredential ? Theme.success : Theme.warning
                                            }

                                            Text {
                                                text: p.baseUrl
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeXs
                                                color: Theme.fgMuted
                                                elide: Text.ElideMiddle
                                                Layout.fillWidth: true
                                            }

                                            AppButton {
                                                variant: "secondary"
                                                implicitHeight: 28
                                                text: qsTr("编辑")
                                                onClicked: {
                                                    root.editingName = p.name
                                                    eName.text = p.name; eName.enabled = false
                                                    eBase.text = p.baseUrl; root.setCombo(eProto, p.protocol)
                                                    eModel.text = p.imageModel; eTimeout.value = p.timeoutSeconds; eKey.text = ""
                                                }
                                            }

                                            AppButton {
                                                variant: "secondary"
                                                implicitHeight: 28
                                                text: qsTr("设密钥")
                                                onClicked: {
                                                    root.editingName = p.name
                                                    eName.text = p.name; eName.enabled = false
                                                    eKey.focus = true
                                                }
                                            }

                                            AppButton {
                                                variant: "ghost"
                                                implicitHeight: 28
                                                text: qsTr("删密钥")
                                                visible: p.hasCredential
                                                onClicked: {
                                                    Profiles.deleteCredential(p.name)
                                                    root.showToast(qsTr("已从 Windows 凭据管理器删除密钥"), "info")
                                                }
                                            }

                                            AppIconButton {
                                                text: "✕"
                                                toolTipText: qsTr("删除此服务商")
                                                onClicked: {
                                                    if (!Profiles.removeProfileByName(p.name))
                                                        root.showToast(Profiles.lastError(), "error")
                                                    else
                                                        root.showToast(qsTr("已移除 Provider"), "info")
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.borderColor
                            }

                            // Provider 编辑表单
                            Text {
                                text: root.editingName.length > 0 ? (qsTr("编辑服务商: ") + root.editingName) : qsTr("新增服务商")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                                font.bold: true
                                color: Theme.fg
                            }

                            GridLayout {
                                columns: 2
                                columnSpacing: Theme.gap
                                rowSpacing: 8
                                Layout.fillWidth: true

                                Label { text: qsTr("名称"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                AppTextField { id: eName; Layout.fillWidth: true; placeholderText: qsTr("服务商标识符，如 custom-openai") }

                                Label { text: qsTr("Base URL"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                AppTextField { id: eBase; Layout.fillWidth: true; placeholderText: "https://api.openai.com" }

                                Label { text: qsTr("协议类型"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                AppComboBox { id: eProto; Layout.fillWidth: true; model: Profiles.protocols() }

                                Label { text: qsTr("默认模型"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                AppTextField { id: eModel; Layout.fillWidth: true; placeholderText: "gpt-image-2" }

                                Label { text: qsTr("超时 (秒)"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                SpinBox { id: eTimeout; from: 0; to: 1800; value: 300; editable: true; Layout.fillWidth: true }

                                Label { text: qsTr("API 密钥"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                AppTextField { id: eKey; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("留空则不修改原有已存凭据") }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap

                                AppButton {
                                    variant: "primary"
                                    text: root.editingName.length > 0 ? qsTr("保存修改") : qsTr("确认新增 Provider")
                                    enabled: eName.text.length > 0 && eBase.text.length > 0
                                    onClicked: {
                                        if (!Profiles.addProfile(eName.text, eBase.text, eProto.currentText, eModel.text, eTimeout.value)) {
                                            root.showToast(Profiles.lastError(), "error"); return
                                        }
                                        if (eKey.text.length > 0 && !Profiles.setCredential(eName.text, eKey.text)) {
                                            root.showToast(Profiles.lastError(), "error"); return
                                        }
                                        root.showToast(qsTr("Provider 已保存"), "success")
                                        root.resetProfileEditor()
                                    }
                                }

                                AppButton {
                                    variant: "ghost"
                                    text: qsTr("取消编辑")
                                    visible: root.editingName.length > 0
                                    onClicked: root.resetProfileEditor()
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    // ---- 2. Retention ----
                    AppCard {
                        Layout.fillWidth: true
                        implicitHeight: setRetCol.implicitHeight + Theme.gapXl * 2

                        ColumnLayout {
                            id: setRetCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            spacing: Theme.gap

                            Text {
                                text: "💾 " + qsTr("存储空间与历史保留限制")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeLg
                                font.bold: true
                                color: Theme.fg
                            }

                            // 存储空间使用指示条
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapSm / 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("本地已用空间: %1 MiB / %2 MiB").arg((Settings.usedBytes / (1024 * 1024)).toFixed(1)).arg(Math.round(Settings.retentionBytes / (1024 * 1024)))
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSm
                                        color: Theme.fg
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        readonly property real pct: (Settings.retentionBytes > 0) ? (Settings.usedBytes / Settings.retentionBytes * 100) : 0
                                        text: pct.toFixed(1) + "%"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeSm
                                        font.bold: true
                                        color: (pct > 85) ? Theme.danger : Theme.accent
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: Theme.surfaceAlt
                                    border.color: Theme.borderColor
                                    border.width: 1

                                    Rectangle {
                                        height: parent.height
                                        radius: 4
                                        width: Math.min(parent.width, Math.max(0, parent.width * ((Settings.retentionBytes > 0) ? (Settings.usedBytes / Settings.retentionBytes) : 0)))
                                        color: (width > parent.width * 0.85) ? Theme.danger : Theme.accent
                                    }
                                }
                            }

                            GridLayout {
                                columns: 2
                                columnSpacing: Theme.gap
                                rowSpacing: 8
                                Layout.fillWidth: true

                                Label { text: qsTr("最多保留任务行数"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                SpinBox {
                                    id: sbRows; from: 1; to: 100000; editable: true; Layout.fillWidth: true
                                    value: Settings.retentionRows
                                    onValueModified: Settings.retentionRows = value
                                }

                                Label { text: qsTr("存储空间上限 (MiB)"); color: Theme.fgMuted; font.family: Theme.fontFamily }
                                SpinBox {
                                    id: sbMiB; from: 64; to: 1024 * 1024; editable: true; Layout.fillWidth: true
                                    value: Math.round(Settings.retentionBytes / (1024 * 1024))
                                    onValueModified: Settings.retentionBytes = value * 1024 * 1024
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap

                                AppButton {
                                    text: qsTr("立即执行配额清理")
                                    onClicked: {
                                        var n = Settings.applyRetention()
                                        root.showToast(qsTr("清理完成，已回收 %1 个旧任务数据").arg(n), "info")
                                    }
                                }

                                Text {
                                    text: qsTr("注：置顶 (pinned ★) 的任务与资产不在清理范围之内。")
                                    color: Theme.fgSubtle
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    // ---- 3. Trusted hosts ----
                    AppCard {
                        Layout.fillWidth: true
                        implicitHeight: setHostCol.implicitHeight + Theme.gapXl * 2

                        ColumnLayout {
                            id: setHostCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            spacing: Theme.gap

                            Text {
                                text: "🛡️ " + qsTr("可信网络主机 (SSRF 防护白名单 · 重启生效)")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeLg
                                font.bold: true
                                color: Theme.fg
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.gapSm
                                visible: Settings.trustedHosts.length > 0

                                Repeater {
                                    model: Settings.trustedHosts
                                    delegate: Rectangle {
                                        height: 28
                                        width: hostRow.implicitWidth + 16
                                        radius: 14
                                        color: Theme.surfaceAlt
                                        border.color: Theme.borderColor
                                        border.width: 1

                                        RowLayout {
                                            id: hostRow
                                            anchors.centerIn: parent
                                            spacing: Theme.gapSm

                                            Text {
                                                text: modelData
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeXs
                                                color: Theme.fg
                                            }

                                            AppIconButton {
                                                text: "✕"
                                                implicitWidth: 18
                                                implicitHeight: 18
                                                toolTipText: qsTr("移除")
                                                onClicked: {
                                                    Settings.removeTrustedHost(modelData)
                                                    root.showToast(qsTr("已从白名单移除主机"), "info")
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: Settings.trustedHosts.length === 0
                                text: qsTr("（当前暂未配置任何额外受信任内网主机）")
                                color: Theme.fgMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap

                                AppTextField {
                                    id: eHost
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("输入允许访问的私有/内网域名或主机名，如 gw.internal")
                                }

                                AppButton {
                                    variant: "primary"
                                    text: qsTr("添加主机")
                                    enabled: eHost.text.trim().length > 0
                                    onClicked: {
                                        Settings.addTrustedHost(eHost.text.trim())
                                        root.showToast(qsTr("已添加至可信网络主机列表 (重启生效)"), "success")
                                        eHost.text = ""
                                    }
                                }
                            }
                        }
                    }

                    // ---- 4. Theme & WebP ----
                    AppCard {
                        Layout.fillWidth: true
                        implicitHeight: setThemeCol.implicitHeight + Theme.gapXl * 2

                        ColumnLayout {
                            id: setThemeCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            spacing: Theme.gap

                            Text {
                                text: "🎨 " + qsTr("界面外观与格式说明")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeLg
                                font.bold: true
                                color: Theme.fg
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap

                                Label {
                                    text: qsTr("切换浅色模式")
                                    color: Theme.fg
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }

                                Switch {
                                    checked: Settings.themeName === "light"
                                    onToggled: Settings.themeName = checked ? "light" : "dark"
                                }

                                Text {
                                    text: Settings.themeName === "light" ? "☀️ " + qsTr("当前为浅色调色板") : "🌙 " + qsTr("当前为深色暗夜调色板")
                                    color: Theme.fgMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.borderColor
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gapSm

                                Text {
                                    text: "ℹ️"
                                    font.pixelSize: Theme.fontSizeSm
                                }

                                Text {
                                    text: qsTr("说明：本机 Qt 未包含独立 WebP 解码动态库，WebP 格式图片仍可完整生成并安全落盘，在应用内无法直接解码时可通过灯箱按「O」键调用系统看图程序查看。")
                                    color: Theme.fgSubtle
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: Theme.gapLg }
                }
            }
        }
    }

    // GUI-G keyboard shortcuts (SPEC 9.1 "键盘直达：生成 / 切模型 / 取消 / 历史导航"). Window-context
    // Shortcuts; each is disabled while the lightbox is open so its own [Esc/←/→/C/S/O] keys win.
    // "历史导航" here = jump to the history view (arrow keys then move within its GridView, and the
    // lightbox wraps through results) -- in-app direct access, not a system-wide hotkey.
    Shortcut {
        sequence: "Ctrl+Return"
        enabled: !Lightbox.open && root.view === 0
        onActivated: root.doGenerate()
    }
    Shortcut {
        sequence: "Ctrl+M"
        enabled: !Lightbox.open && root.view === 0
        onActivated: root.cycleModel()
    }
    Shortcut {
        sequence: "Ctrl+P"
        enabled: !Lightbox.open && root.view === 0
        onActivated: root.cycleProvider()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !Lightbox.open && root.busy
        onActivated: root.doCancel()
    }
    Shortcut {
        sequence: "Ctrl+H"
        enabled: !Lightbox.open
        onActivated: root.view = 1
    }

    // Reference intake: the multi-select picker behind "选择图片…". The dialog itself was lost
    // in the Theme refactor while the call site survived, so the button threw ReferenceError.
    FileDialog {
        id: fileDialog
        title: qsTr("选择参考图")
        nameFilters: [qsTr("图片文件 (*.png *.jpg *.jpeg *.webp *.bmp)"), qsTr("所有文件 (*)")]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            // Qt 6.8's QtQuick.Dialogs FileDialog exposes selectedFiles (a list of urls) for
            // multi-select -- NOT selectedUrls, which does not exist on this type.
            for (var i = 0; i < fileDialog.selectedFiles.length; ++i)
                root.stageReference(fileDialog.selectedFiles[i].toString())
        }
    }

    // GUI-D lightbox: binds to the Lightbox controller; opens when a result thumbnail is clicked.
    LightboxOverlay { }

    // Modern floating toast notification banner
    AppToast {
        id: toast
    }
}
