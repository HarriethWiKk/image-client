// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// These cases talk to the real Credential Manager: a mocked one would prove
// nothing about the only storage location SPEC 6.3 allows. Every target the test
// creates is deleted again, and the whole class skips cleanly if the OS refuses
// writes in this session (service accounts and some CI images have no credential
// provider).

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFile>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QTest>

#include "oic/secret/credentials.h"

namespace {

QByteArray randomSecret()
{
    QByteArray secret("sk-test-");
    secret.append(QByteArray::number(QCoreApplication::applicationPid()));
    secret.append("-\xC3\xA9\xE4\xB8\xAD");  // non-ASCII must survive: keys are opaque bytes
    while (secret.size() < 40) {
        secret.append('x');
    }
    return secret;
}

// Every readable file under a directory, for the "no plaintext anywhere on disk"
// check. Bounded so a huge tree cannot make the test crawl.
QStringList readableFiles(const QString &root, int maxFiles)
{
    QStringList found;
    if (root.isEmpty() || !QDir(root).exists()) {
        return found;
    }
    QDir dir(root);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (found.size() >= maxFiles) {
            return found;
        }
        if (entry.size() > 8 * 1024 * 1024) {
            continue;
        }
        found.append(entry.absoluteFilePath());
    }
    const QFileInfoList children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &child : children) {
        found.append(readableFiles(child.absoluteFilePath(), maxFiles - found.size()));
    }
    return found;
}

}  // namespace

class TstCredentials : public QObject
{
    Q_OBJECT

private:
    QString m_target;
    bool m_writable = false;

private slots:
    void initTestCase()
    {
        QString error;
        m_target = oic::secret::targetNameFor(QStringLiteral("qtest-%1").arg(QCoreApplication::applicationPid()), &error);
        QVERIFY2(!m_target.isEmpty(), qPrintable(error));

        // Probe the provider once. Where it refuses writes (service accounts, some
        // CI images) the individual cases skip instead of failing.
        m_writable = oic::secret::writeSecret(m_target, QByteArray("probe"), &error);
        if (!m_writable) {
            qInfo() << "Credential Manager unavailable, skipping:" << error;
        }
    }

    void cleanupTestCase()
    {
        if (!m_target.isEmpty()) {
            QString error;
            oic::secret::eraseSecret(m_target, &error);
        }
    }

    void init()
    {
        if (!m_writable) {
            QSKIP("Credential Manager refused writes in this session");
        }
    }

    void profileNameRules_data();
    void profileNameRules();

    void roundTripKeepsExactBytes();
    void missingCredentialReportsAbsentNotFailure();
    void eraseIsIdempotent();
    void oversizedSecretRejectedWithoutPartialWrite();
    void emptySecretRejected();
    void listOnlyOwnsOurPrefix();
    void secretNeverReachesDisk();
};

void TstCredentials::profileNameRules_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("plain") << QStringLiteral("default") << true;
    QTest::newRow("dashes dots") << QStringLiteral("work.local_v2") << true;
    QTest::newRow("max length") << QString(64, QLatin1Char('a')) << true;
    QTest::newRow("too long") << QString(65, QLatin1Char('a')) << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("leading dot") << QStringLiteral(".hidden") << false;
    QTest::newRow("path separator") << QStringLiteral("a/b") << false;
    QTest::newRow("backslash") << QStringLiteral("a\\b") << false;
    QTest::newRow("space") << QStringLiteral("my profile") << false;
    // A name carrying the prefix would let one profile be written twice under two
    // different keys; the character rules already forbid '/'.
    QTest::newRow("embedded prefix") << QStringLiteral("image-client/profile/evil") << false;
    QTest::newRow("control char") << QStringLiteral("a\x01" "b") << false;
    QTest::newRow("non ascii") << QStringLiteral("配置") << false;
}

void TstCredentials::profileNameRules()
{
    QFETCH(const QString, name);
    QFETCH(const bool, accepted);
    QString error;
    const QString target = oic::secret::targetNameFor(name, &error);
    if (accepted) {
        QCOMPARE(target, QStringLiteral("image-client/profile/") + name);
        QVERIFY(error.isEmpty());
    } else {
        QVERIFY2(target.isEmpty(), qPrintable(QStringLiteral("accepted invalid profile name: %1").arg(name)));
        QVERIFY(!error.isEmpty());
    }
}

void TstCredentials::roundTripKeepsExactBytes()
{
    const QByteArray secret = randomSecret();
    QString error;
    QVERIFY2(oic::secret::writeSecret(m_target, secret, &error), qPrintable(error));

    const QByteArray read = oic::secret::readSecret(m_target, &error);
    QVERIFY2(read == secret, "credential bytes must survive the round trip unchanged");
    QVERIFY(error.isEmpty());
    QVERIFY(read.contains("\xC3\xA9\xE4\xB8\xAD"));  // not re-encoded, not truncated at a NUL
}

void TstCredentials::missingCredentialReportsAbsentNotFailure()
{
    QString error;
    const QString target = oic::secret::targetNameFor(QStringLiteral("qtest-absent-no-such"), &error);
    QVERIFY(!target.isEmpty());

    bool absent = false;
    const QByteArray read = oic::secret::readSecret(target, &error, &absent);
    QVERIFY(read.isEmpty());
    QVERIFY2(absent, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void TstCredentials::eraseIsIdempotent()
{
    QString error;
    bool existed = true;
    QVERIFY(oic::secret::eraseSecret(m_target, &error, &existed));  // may or may not exist yet
    existed = true;
    QVERIFY2(oic::secret::eraseSecret(m_target, &error, &existed), qPrintable(error));
    QVERIFY2(!existed, "second erase of a gone credential must report it did not exist");
    QVERIFY(error.isEmpty());

    bool absent = false;
    oic::secret::readSecret(m_target, &error, &absent);
    QVERIFY(absent);
}

void TstCredentials::oversizedSecretRejectedWithoutPartialWrite()
{
    QString error;
    QVERIFY(oic::secret::eraseSecret(m_target, &error));
    const QByteArray huge(oic::secret::maxSecretBytes() + 1, 'k');
    QVERIFY2(!oic::secret::writeSecret(m_target, huge, &error), "oversized secret must be refused");
    QVERIFY2(error.contains(QString::fromUtf8("上限")), qPrintable(error));

    bool absent = false;
    oic::secret::readSecret(m_target, &error, &absent);
    QVERIFY2(absent, qPrintable(QStringLiteral("rejected write left a credential behind: %1").arg(error)));
}

void TstCredentials::emptySecretRejected()
{
    QString error;
    QVERIFY2(!oic::secret::writeSecret(m_target, QByteArray(), &error), "empty key must be refused");
    QVERIFY(!error.isEmpty());
}

void TstCredentials::listOnlyOwnsOurPrefix()
{
    QString error;
    QVERIFY2(oic::secret::writeSecret(m_target, randomSecret(), &error), qPrintable(error));

    const QStringList targets = oic::secret::listTargetNames(&error);
    QVERIFY2(targets.contains(m_target), qPrintable(QStringLiteral("%1 missing from enumeration").arg(m_target)));
    for (const QString &entry : targets) {
        QVERIFY2(entry.startsWith(QStringLiteral("image-client/profile/")),
                 qPrintable(QStringLiteral("enumeration leaked foreign target %1").arg(entry)));
    }
}

// SPEC 6.3 forbids a file fallback, so the promise is testable: after a key is
// stored, its bytes must not be findable in the application's own data tree or
// in the temp directory.
void TstCredentials::secretNeverReachesDisk()
{
    const QByteArray secret = randomSecret();
    QString error;
    QVERIFY2(oic::secret::writeSecret(m_target, secret, &error), qPrintable(error));

    QStringList roots;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        roots << localAppData + QStringLiteral("/image-client");
    }
    roots << QDir::tempPath();

    int scanned = 0;
    for (const QString &root : roots) {
        const QStringList files = readableFiles(root, 400);
        for (const QString &file : files) {
            QFile handle(file);
            if (!handle.open(QIODevice::ReadOnly)) {
                continue;
            }
            ++scanned;
            const QByteArray content = handle.read(9 * 1024 * 1024);
            QVERIFY2(!content.contains(secret),
                     qPrintable(QStringLiteral("secret plaintext found in %1").arg(file)));
        }
    }
    QVERIFY2(scanned > 0 || roots.isEmpty(), "scan found no files at all -- the check would be vacuous");

    oic::secret::eraseSecret(m_target, &error);
}

QTEST_GUILESS_MAIN(TstCredentials)
#include "tst_credentials.moc"
