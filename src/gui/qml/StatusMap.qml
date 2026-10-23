// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
pragma Singleton
import QtQuick

// Single source of truth for job-status labels and colors. The badge and the history
// thumbnail placeholder used to carry two copies that had drifted (succeeded was indigo in
// one and green in the other).
QtObject {
    function label(status) {
        if (status === "succeeded") return qsTr("成功")
        if (status === "failed") return qsTr("失败")
        if (status === "cancelled") return qsTr("已取消")
        if (status === "running") return qsTr("生成中")
        if (status === "queued") return qsTr("排队")
        return status
    }

    function color(status) {
        if (status === "succeeded") return Theme.success
        if (status === "failed") return Theme.danger
        if (status === "running") return Theme.accent
        if (status === "queued") return Theme.warning
        return Theme.fgMuted
    }

    function background(status) {
        if (status === "succeeded") return Theme.successSubtle
        if (status === "failed") return Theme.dangerSubtle
        if (status === "running") return Theme.accentSubtle
        if (status === "queued") return Theme.warningSubtle
        return Theme.surfaceAlt
    }
}
