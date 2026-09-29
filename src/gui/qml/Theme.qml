// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
pragma Singleton
import QtQuick

QtObject {
    id: theme

    // Palette switches on Settings.themeName (persisted). Guarded against undefined context property.
    readonly property bool dark: (typeof Settings !== "undefined" && Settings) ? (Settings.themeName !== "light") : true

    // Surface colors (4-tier hierarchy)
    readonly property color bg: dark ? "#121214" : "#f5f6f8"
    readonly property color surface: dark ? "#1a1a1e" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#24252a" : "#f0f1f4"
    readonly property color surfaceHover: dark ? "#2e2f37" : "#e6e7ed"
    readonly property color borderColor: dark ? "#2d2e35" : "#e1e2e8"
    readonly property color borderLight: dark ? "#3a3b44" : "#d1d3dc"

    // Text & Foreground
    readonly property color fg: dark ? "#ededf0" : "#18181b"
    readonly property color fgMuted: dark ? "#9898a4" : "#6b6c7a"
    readonly property color fgSubtle: dark ? "#646573" : "#9ca3af"

    // Accents & Semantics
    readonly property color accent: "#4f46e5"          // Indigo 600
    readonly property color accentHover: "#6366f1"     // Indigo 500
    readonly property color accentPressed: "#4338ca"   // Indigo 700
    readonly property color accentFg: "#ffffff"
    readonly property color accentSubtle: dark ? "#22204c" : "#e0e7ff"

    readonly property color danger: dark ? "#ef4444" : "#dc2626"
    readonly property color dangerHover: dark ? "#f87171" : "#b91c1c"
    readonly property color dangerFg: "#ffffff"
    readonly property color dangerSubtle: dark ? "#3a1d1f" : "#fee2e2"

    readonly property color success: dark ? "#10b981" : "#059669"
    readonly property color successSubtle: dark ? "#133827" : "#d1fae5"

    readonly property color warning: dark ? "#f59e0b" : "#d97706"
    readonly property color warningSubtle: dark ? "#382912" : "#fef3c7"

    // Geometry
    readonly property real radiusSm: 6
    readonly property real radius: 10
    readonly property real radiusLg: 14
    readonly property real gapXs: 2
    readonly property real gapSm: 8
    readonly property real gap: 12
    readonly property real gapLg: 20
    readonly property real gapXl: 16

    // Layout constants
    readonly property real scrollBarWidth: 12
    readonly property real cardMinWidth: 224
    readonly property real topBarHeight: 48
    readonly property real touchTargetMin: 44

    // Typography
    readonly property string fontFamily: "Segoe UI, -apple-system, PingFang SC, Microsoft YaHei, sans-serif"
    readonly property int fontSizeXs: 11
    readonly property int fontSizeSm: 12
    readonly property int fontSize: 13
    readonly property int fontSizeLg: 15
    readonly property int fontSizeXl: 18
    readonly property int fontSizeHero: 44

    // Durations
    readonly property int animFast: 120
    readonly property int animNormal: 200
}
