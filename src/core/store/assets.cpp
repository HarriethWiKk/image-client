// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/store/assets.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <vector>

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>

namespace oic::store {
namespace {

std::wstring toWide(const QString &value)
{
    return std::wstring(reinterpret_cast<const wchar_t *>(value.utf16()), size_t(value.size()));
}

QString sidToString(PSID sid)
{
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text)) {
        return QString();
    }
    QString result = QString::fromWCharArray(text);
    LocalFree(text);
    return result;
}

bool addWellKnownSid(std::vector<QString> *out, WELL_KNOWN_SID_TYPE type)
{
    DWORD size = 0;
    CreateWellKnownSid(type, nullptr, nullptr, &size);
    std::vector<BYTE> buffer(size);
    if (!CreateWellKnownSid(type, nullptr, buffer.data(), &size)) {
        return false;
    }
    const QString text = sidToString(reinterpret_cast<PSID>(buffer.data()));
    if (text.isEmpty()) {
        return false;
    }
    out->push_back(text);
    return true;
}

QStringList defaultTrusteeSids()
{
    std::vector<QString> sids;

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> buffer(size);
        auto *user = reinterpret_cast<PTOKEN_USER>(buffer.data());
        if (GetTokenInformation(token, TokenUser, user, size, &size) && user != nullptr) {
            const QString text = sidToString(user->User.Sid);
            if (!text.isEmpty()) {
                sids.push_back(text);
            }
        }
        CloseHandle(token);
    }
    addWellKnownSid(&sids, WinLocalSystemSid);
    addWellKnownSid(&sids, WinBuiltinAdministratorsSid);

    QStringList result;
    for (const QString &sid : sids) {
        result.append(sid);
    }
    return result;
}

}  // namespace

// ---------------------------------------------------------------- lock

QuotaLock::QuotaLock(QString name) : m_name(std::move(name)) {}

QuotaLock::~QuotaLock()
{
    release();
    if (m_handle != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
}

bool QuotaLock::acquire(int timeoutMs, QString *error)
{
    if (m_held) {
        return true;
    }
    if (m_handle == nullptr) {
        m_handle = CreateMutexW(nullptr, FALSE, toWide(m_name).c_str());
        if (m_handle == nullptr) {
            if (error != nullptr) {
                *error = QStringLiteral("创建命名互斥量失败（Win32 错误 %1）").arg(GetLastError());
            }
            return false;
        }
    }

    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(m_handle), DWORD(timeoutMs));
    // WAIT_ABANDONED_0 means a previous owner died holding it; taking over is the
    // reason to use a mutex instead of a lock file, which would linger forever.
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED_0) {
        m_held = true;
        return true;
    }
    if (error != nullptr) {
        *error = wait == WAIT_TIMEOUT ? QStringLiteral("等待资产配额锁超时")
                                      : QStringLiteral("获取资产配额锁失败（Win32 错误 %1）").arg(wait);
    }
    return false;
}

void QuotaLock::release()
{
    if (m_held && m_handle != nullptr) {
        ReleaseMutex(static_cast<HANDLE>(m_handle));
    }
    m_held = false;
}

// ---------------------------------------------------------------- ids and ACL

bool isSafePathComponent(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$"));
    return pattern.match(value).hasMatch();
}

AclVerdict evaluateGrants(const QStringList &grantSids, const QStringList &allowedSids)
{
    AclVerdict verdict;
    for (const QString &sid : grantSids) {
        verdict.grants.append(sid);
        if (allowedSids.contains(sid)) {
            continue;
        }
        // Authority 15 is the app-container capability family Windows attaches to
        // parts of the profile. A sandbox handle, not another user's read path.
        if (sid.startsWith(QLatin1String("S-1-15-"))) {
            verdict.notes.append(sid);
            continue;
        }
        // Anything else is unrecognised, and an unrecognised grant is exactly what
        // this check exists to catch, so it fails rather than passing quietly.
        verdict.offenders.append(sid);
    }
    verdict.ok = !grantSids.isEmpty() && verdict.offenders.isEmpty();
    return verdict;
}

AclReport checkDirectoryAcl(const QString &path)
{
    AclReport report;
    report.allowedSids = defaultTrusteeSids();
    if (report.allowedSids.isEmpty()) {
        report.error = QStringLiteral("无法确定当前用户与系统账户的 SID");
        return report;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    const DWORD result = GetNamedSecurityInfoW(toWide(path).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS) {
        report.error = QStringLiteral("读取 %1 的 DACL 失败（Win32 错误 %2）").arg(path).arg(result);
        if (descriptor != nullptr) {
            LocalFree(descriptor);
        }
        return report;
    }

    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted)) {
        report.error = QStringLiteral("解析 %1 的 DACL 失败").arg(path);
        LocalFree(descriptor);
        return report;
    }
    if (!present || dacl == nullptr) {
        // A NULL DACL is not "no permissions" -- it is unrestricted access, so
        // this has to be a hard failure rather than an empty grant list.
        report.error = QStringLiteral("%1 的 DACL 为空（NULL），等于对所有账户开放").arg(path);
        LocalFree(descriptor);
        return report;
    }

    const DWORD count = dacl->AceCount;
    QStringList grants;
    QStringList denies;
    for (DWORD i = 0; i < count; ++i) {
        ACE_HEADER *ace = nullptr;
        if (!GetAce(dacl, i, reinterpret_cast<void **>(&ace)) || ace == nullptr) {
            continue;
        }
        if (ace->AceType == ACCESS_ALLOWED_ACE_TYPE) {
            auto *allowed = reinterpret_cast<PACCESS_ALLOWED_ACE>(ace);
            const QString sid = sidToString(reinterpret_cast<PSID>(&allowed->SidStart));
            if (!sid.isEmpty()) {
                grants.append(sid);
            }
        } else if (ace->AceType == ACCESS_DENIED_ACE_TYPE || ace->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE) {
            denies.append(QStringLiteral("deny#%1").arg(i));
        }
        // SYSTEM_MANDATORY_LABEL_ACE_TYPE and the OBJECT variants are integrity
        // labels, not grants to an account, so they are not counted either way.
    }

    report.verdict = evaluateGrants(grants, report.allowedSids);
    if (!denies.isEmpty()) {
        report.verdict.ok = false;
        report.verdict.offenders.append(denies);
    }
    report.ok = report.verdict.ok;
    if (!report.ok && report.error.isEmpty()) {
        report.error = QStringLiteral("%1 的 ACL 含预期之外的授权账户：%2")
                           .arg(path, report.verdict.offenders.join(QStringLiteral(", ")));
    }
    LocalFree(descriptor);
    return report;
}

// ---------------------------------------------------------------- store

AssetStore::AssetStore(QString assetsRoot, qint64 quotaBytes, int lockTimeoutMs)
    : m_root(std::move(assetsRoot)), m_quotaBytes(quotaBytes), m_lockTimeoutMs(lockTimeoutMs)
{
}

QString AssetStore::extensionFor(const QString &mime)
{
    // Content-Type legitimately carries parameters ("image/jpeg; charset=x"), so
    // match on the media type alone.
    QString lower = mime.trimmed().toLower();
    const int separator = lower.indexOf(QLatin1Char(';'));
    if (separator >= 0) {
        lower = lower.left(separator).trimmed();
    }
    if (lower == QLatin1String("image/png")) {
        return QStringLiteral("png");
    }
    if (lower == QLatin1String("image/jpeg") || lower == QLatin1String("image/jpg")) {
        return QStringLiteral("jpg");
    }
    if (lower == QLatin1String("image/webp")) {
        return QStringLiteral("webp");
    }
    if (lower == QLatin1String("image/gif")) {
        return QStringLiteral("gif");
    }
    return QStringLiteral("bin");
}

qint64 AssetStore::usedBytes(QString *error) const
{
    qint64 total = 0;
    QDirIterator it(m_root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.filePath() == QDir::currentPath()) {
            continue;
        }
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink()) {
            continue;  // never count or follow what we did not write
        }
        total += info.size();
    }
    if (error != nullptr) {
        error->clear();
    }
    return total;
}

AssetWriteResult AssetStore::write(const QString &jobId, const QString &assetId, const QString &mime,
                                   const QByteArray &bytes) const
{
    AssetWriteResult result;
    if (!isSafePathComponent(jobId) || !isSafePathComponent(assetId)) {
        result.error = QStringLiteral("任务或资源 id 含非法字符");
        return result;
    }
    if (bytes.isEmpty()) {
        result.error = QStringLiteral("资源内容为空");
        return result;
    }
    if (m_quotaBytes > 0 && bytes.size() > m_quotaBytes) {
        result.error = QStringLiteral("单张图 %1 字节已超过总配额 %2 字节").arg(bytes.size()).arg(m_quotaBytes);
        return result;
    }

    QuotaLock lock;
    QString lockError;
    if (!lock.acquire(m_lockTimeoutMs, &lockError)) {
        result.error = lockError;
        return result;
    }

    const qint64 used = usedBytes(nullptr);
    if (m_quotaBytes > 0 && used + bytes.size() > m_quotaBytes) {
        result.error = QStringLiteral("资产目录已用 %1 字节，写入 %2 字节会超过配额 %3")
                           .arg(used)
                           .arg(bytes.size())
                           .arg(m_quotaBytes);
        return result;  // nothing created yet, so nothing to clean up
    }

    const QDir root(m_root);
    if (!root.exists() && !QDir().mkpath(m_root)) {
        result.error = QStringLiteral("无法创建资产根目录 %1").arg(m_root);
        return result;
    }
    const QString jobDir = root.absoluteFilePath(jobId);
    const bool creating = !QDir(jobDir).exists();
    if (creating && !QDir().mkpath(jobDir)) {
        result.error = QStringLiteral("无法创建任务目录 %1").arg(jobDir);
        return result;
    }
    {
        // SPEC 6.1: the old build created directories with no explicit mode and
        // left them world-readable. Verify the inherited ACL, and repair it when it
        // is broader than this user -- measured on a scratch tree under D:\, the
        // inherited grants include Authenticated Users and BUILTIN\Users, so a
        // check-only design would make saving images impossible there. Runs on
        // existing directories too: a job dir created before this policy existed is
        // exactly the one that may be loose.
        AclReport acl = checkDirectoryAcl(jobDir);
        if (!acl.ok) {
            QString fixError;
            if (!enforcePrivateAcl(jobDir, &fixError)) {
                if (creating) {
                    QDir(jobDir).removeRecursively();
                }
                result.error = fixError;
                return result;
            }
            acl = checkDirectoryAcl(jobDir);
            if (!acl.ok) {
                if (creating) {
                    QDir(jobDir).removeRecursively();
                }
                result.error = QStringLiteral("改写后 %1 的 ACL 仍含越权账户：%2")
                                   .arg(jobDir, acl.verdict.offenders.join(QStringLiteral(", ")));
                return result;
            }
        }
    }

    const QString filename = assetId + QLatin1Char('.') + extensionFor(mime);
    const QDir job(jobDir);
    const QString absolute = job.absoluteFilePath(filename);
    result.relPath = jobId + QLatin1Char('/') + filename;
    result.absolutePath = absolute;

    QFile file(absolute);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("写入 %1 失败：%2").arg(absolute, file.errorString());
        return result;
    }
    if (file.write(bytes) != bytes.size()) {
        const QString error = file.errorString();
        file.close();
        file.remove();  // a truncated image looks like a saved one
        result.error = QStringLiteral("写入 %1 不完整：%2").arg(absolute, error);
        return result;
    }
    file.flush();
    file.close();

    result.bytes = QFileInfo(absolute).size();
    if (result.bytes != bytes.size()) {
        QFile::remove(absolute);
        result.error = QStringLiteral("落盘字节数与内存内容不一致");
        result.bytes = 0;
        return result;
    }
    return result;
}

bool AssetStore::remove(const QString &relPath, QString *error) const
{
    const QString cleaned = QDir::cleanPath(relPath);
    if (cleaned.startsWith(QStringLiteral("..")) || cleaned.contains(QStringLiteral("/../"))
        || QDir::isAbsolutePath(relPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("拒绝删除目录外的路径：%1").arg(relPath);
        }
        return false;
    }

    const QDir root(m_root);
    const QString absolute = root.absoluteFilePath(cleaned);
    // Resolve both sides before comparing: a junction or symlink inside the tree
    // must not turn relPath into a delete outside it.
    const QString canonicalRoot = QFileInfo(m_root).canonicalFilePath();
    const QString canonicalTarget = QFileInfo(absolute).canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalTarget.isEmpty()
        || !canonicalTarget.startsWith(canonicalRoot + QLatin1Char('/'))) {
        if (error != nullptr) {
            *error = QStringLiteral("解析后的路径逃出了资产根目录：%1").arg(absolute);
        }
        return false;
    }
    if (!QFile::remove(absolute)) {
        if (error != nullptr) {
            *error = QStringLiteral("删除 %1 失败").arg(absolute);
        }
        return false;
    }
    return true;
}

bool enforcePrivateAcl(const QString &path, QString *error)
{
    // Buffers outlive the PSIDs that point into them: std::vector's heap block is
    // transferred, not copied, when the outer vector grows.
    std::vector<std::vector<BYTE>> storage;
    std::vector<PSID> trustees;

    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        storage.emplace_back(size);
        auto *user = reinterpret_cast<PTOKEN_USER>(storage.back().data());
        if (GetTokenInformation(token, TokenUser, user, size, &size)) {
            trustees.push_back(user->User.Sid);
        } else {
            storage.pop_back();
        }
        CloseHandle(token);
    }

    auto addKnown = [&storage, &trustees](WELL_KNOWN_SID_TYPE type) {
        DWORD size = 0;
        CreateWellKnownSid(type, nullptr, nullptr, &size);
        storage.emplace_back(size);
        if (CreateWellKnownSid(type, nullptr, storage.back().data(), &size)) {
            trustees.push_back(reinterpret_cast<PSID>(storage.back().data()));
        } else {
            storage.pop_back();
        }
    };
    addKnown(WinLocalSystemSid);
    addKnown(WinBuiltinAdministratorsSid);

    if (trustees.empty()) {
        if (error != nullptr) {
            *error = QStringLiteral("无法取得本机账户的 SID，拒绝改写 ACL");
        }
        return false;
    }

    std::vector<EXPLICIT_ACCESSW> entries;
    for (PSID sid : trustees) {
        EXPLICIT_ACCESSW entry = {};
        entry.grfAccessPermissions = FILE_ALL_ACCESS;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
        BuildTrusteeWithSidW(&entry.Trustee, sid);
        entries.push_back(entry);
    }

    PACL acl = nullptr;
    if (SetEntriesInAclW(static_cast<ULONG>(entries.size()), entries.data(), nullptr, &acl) != ERROR_SUCCESS
        || acl == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("构造私有 DACL 失败");
        }
        return false;
    }

    std::wstring target = toWide(path);
    // PROTECTED_... drops the inherited entries; without it a loose parent would
    // keep leaking Authenticated Users / BUILTIN\Users through the grant we add.
    const ULONG result = SetNamedSecurityInfoW(target.data(), SE_FILE_OBJECT,
                                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                              nullptr, acl, nullptr);
    LocalFree(acl);
    if (result != ERROR_SUCCESS) {
        if (error != nullptr) {
            *error = QStringLiteral("设置 %1 的私有 DACL 失败（Win32 错误 %2）").arg(path).arg(result);
        }
        return false;
    }
    return true;
}

}  // namespace oic::store
