// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDebug>

#include "oic/app/assetimageprovider.h"
#include "oic/app/backend.h"
#include "oic/app/historymodel.h"
#include "oic/app/jobcontroller.h"
#include "oic/app/lightboxcontroller.h"
#include "oic/app/modelscontroller.h"
#include "oic/app/profilecontroller.h"
#include "oic/app/settingscontroller.h"
#include "oic/store/paths.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("image-client"));
    app.setApplicationName(QStringLiteral("image-client"));

    // Pinned rather than left to the platform default so the look is identical from a
    // dev shell and from a packaged build (SPEC 2.2).
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Single instance: a second launch is a no-op (the first holds the lock). The lock is
    // GUI-only by design (SPEC 2.2: the mcp exe never gates on it) and lives for main()'s
    // whole scope, so it releases on exit -- and QLockFile breaks it if the owner crashed.
    const oic::store::Paths paths = oic::store::resolvePaths();
    QLockFile singleInstanceLock(paths.ok() ? paths.root + QStringLiteral("/image-client.lock")
                                            : QStringLiteral("image-client.lock"));
    if (paths.ok()) {
        QDir().mkpath(paths.root);
        if (!singleInstanceLock.tryLock(0)) {
            qWarning("image-client: 已有实例在运行,本次退出");
            return 0;
        }
    }

    oic::app::Backend backend;
    QString initError;
    if (!backend.init(&initError))
        qWarning("image-client: backend init failed: %s", qPrintable(initError));

    // Controllers are constructed before the engine and parented to the backend, so the
    // engine is torn down first while they are still alive (no dangling context property).
    oic::app::ProfileController profiles(&backend, &backend);
    oic::app::HistoryModel history(&backend, &backend);
    oic::app::JobController jobs(&backend, &backend);
    oic::app::SettingsController settings(&backend, &backend);
    oic::app::LightboxController lightbox(&backend);
    oic::app::ModelsController models(&backend, &backend);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("asset"),
                            new oic::app::AssetImageProvider(backend.assetsRoot()));

    QQmlContext *root = engine.rootContext();
    root->setContextProperty(QStringLiteral("Backend"), &backend);
    root->setContextProperty(QStringLiteral("Profiles"), &profiles);
    root->setContextProperty(QStringLiteral("History"), &history);
    root->setContextProperty(QStringLiteral("Jobs"), &jobs);
    root->setContextProperty(QStringLiteral("Settings"), &settings);
    root->setContextProperty(QStringLiteral("Lightbox"), &lightbox);
    root->setContextProperty(QStringLiteral("Models"), &models);

    engine.loadFromModule("App", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
