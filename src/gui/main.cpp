// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("image-client"));
    app.setApplicationName(QStringLiteral("image-client"));

    // Pinned rather than left to platform default so the look is identical
    // from a dev shell and from a packaged build.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("App", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
