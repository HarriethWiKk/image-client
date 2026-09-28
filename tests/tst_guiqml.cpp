// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// GUI-C view/data binding proof, run offscreen so it needs no eyeballs.
//
// The reference-intake regressions all came down to one thing: after the controller staged a
// reference, the QML Repeater over the reference list did not repaint until some later action
// forced it. That is a binding problem, and a binding problem is testable -- so this loads a
// Repeater bound to JobController::referenceSources and asserts its live delegate count tracks
// every add/remove. It is the mechanism the drop / file-dialog / paste paths share (they all
// route through addReferencePath), so proving it here covers the part the manual smoke kept
// catching and I kept not reproducing in code.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "oic/app/jobcontroller.h"
#include "oic/app/lightboxcontroller.h"

namespace {

// The QML Repeater's live delegate count. QQuickRepeater has no public header in Qt 6.8.3
// (private-only), so locate the object by name and read its exported `count` property.
int repeaterCount(QObject *root)
{
    QObject *repeater = root->findChild<QObject *>(QStringLiteral("refs"));
    if (repeater == nullptr)
        return -1;
    return repeater->property("count").toInt();
}

}  // namespace

class TstGuiQml : public QObject
{
    Q_OBJECT

private slots:
    void repeaterTracksReferenceSources();
    void popupOpensWhenControllerOpens();
};

void TstGuiQml::repeaterTracksReferenceSources()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto makePng = [&](const QString &name) -> QString {
        // No QVERIFY inside a value-returning lambda: its failure path is a bare `return;`.
        const QString path = QDir(dir.path()).filePath(name);
        QImage image(20, 10, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        image.save(path, "PNG");
        return path;
    };
    const QString first = makePng(QStringLiteral("a.png"));
    const QString second = makePng(QStringLiteral("b.png"));

    // addReferencePath only reads + probes + stages a file, so a null backend is fine here.
    oic::app::JobController jobs(nullptr);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Jobs"), &jobs);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\n"
        "Item {\n"
        "    Repeater {\n"
        "        objectName: \"refs\"\n"
        "        model: Jobs.referenceSources\n"
        "        delegate: Rectangle { objectName: \"thumb\"; width: 8; height: 8 }\n"
        "    }\n"
        "}\n",
        QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY(!root.isNull());
    QVERIFY(root->findChild<QObject *>(QStringLiteral("refs")) != nullptr);
    QCOMPARE(repeaterCount(root.data()), 0);

    const QString sourceA = jobs.addReferencePath(QUrl::fromLocalFile(first).toString());
    QVERIFY2(!sourceA.isEmpty(), qPrintable(jobs.lastError()));
    QCoreApplication::processEvents();
    // The decisive assertion: the view reflects the model with no manual repaint.
    QCOMPARE(repeaterCount(root.data()), 1);

    QVERIFY(!jobs.addReferencePath(QUrl::fromLocalFile(second).toString()).isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(repeaterCount(root.data()), 2);

    // A rejected file (not an image) changes nothing in the model, so the view must not grow.
    const QString notePath = QDir(dir.path()).filePath(QStringLiteral("note.txt"));
    QFile note(notePath);
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("not an image");
    note.close();
    QVERIFY(jobs.addReferencePath(notePath).isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(repeaterCount(root.data()), 2);

    QVERIFY(jobs.removeReference(sourceA));
    QCoreApplication::processEvents();
    QCOMPARE(repeaterCount(root.data()), 1);

    jobs.clearReferences();
    QCoreApplication::processEvents();
    QCOMPARE(repeaterCount(root.data()), 0);
}

void TstGuiQml::popupOpensWhenControllerOpens()
{
    // Proves the overlay-open binding (Popup parented to Overlay.overlay, visible bound to the
    // controller's `open`) works IN A WINDOW -- exactly the state a thumbnail click puts the app
    // in. A Popup cannot be instantiated headlessly with a window via QQmlComponent::create()
    // (no Overlay.overlay), so this mirrors the real LightboxOverlay's bindings inline rather than
    // loading the file; the file's parse/load is separately covered by the offscreen exe smoke.
    oic::app::LightboxController lb;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Lightbox"), &lb);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\n"
        "import QtQuick.Controls.Basic\n"
        "Window {\n"
        "    width: 400; height: 300; visible: true\n"
        "    Popup {\n"
        "        objectName: \"pop\"\n"
        "        parent: Overlay.overlay\n"
        "        modal: true\n"
        "        visible: Lightbox.open\n"
        "        width: parent ? parent.width : 0\n"
        "        height: parent ? parent.height : 0\n"
        "    }\n"
        "}\n",
        QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY(!root.isNull());
    QObject *popup = root->findChild<QObject *>(QStringLiteral("pop"));
    QVERIFY(popup != nullptr);
    QVERIFY(!popup->property("visible").toBool());

    lb.setModel({QStringLiteral("a"), QStringLiteral("b")});
    lb.openAt(0);
    QCoreApplication::processEvents();
    QCOMPARE(popup->property("visible").toBool(), true);

    lb.close();
    QCoreApplication::processEvents();
    QCOMPARE(popup->property("visible").toBool(), false);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");  // QML engine needs a GUI app but no real window
    QGuiApplication app(argc, argv);
    return QTest::qExec(new TstGuiQml(), argc, argv);
}

#include "tst_guiqml.moc"
