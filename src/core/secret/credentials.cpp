// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/secret/credentials.h"

#include <QRegularExpression>

#include <windows.h>

#include <wincred.h>

namespace oic::secret {
namespace {

constexpr const char *kTargetPrefix = "image-client/profile/";

QString winError(const wchar_t *function, DWORD code)
{
    return QStringLiteral("%1 失败（Win32 错误 %2）").arg(QString::fromStdWString(function)).arg(code);
}

std::wstring toWide(const QString &value)
{
    return std::wstring(reinterpret_cast<const wchar_t *>(value.utf16()), size_t(value.size()));
}

bool profileNameIsUsable(const QString &name)
{
    // Restrictive on purpose: this string becomes a global credential key, so it
    // must not be able to contain a path separator, a control character, or an
    // embedded copy of the prefix.
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(name).hasMatch();
}

}  // namespace

int maxSecretBytes()
{
    return int(CRED_MAX_CREDENTIAL_BLOB_SIZE);
}

QString targetNameFor(const QString &profileName, QString *error)
{
    if (!profileNameIsUsable(profileName)) {
        if (error != nullptr) {
            *error = QStringLiteral("profile 名称只能由字母、数字、点、下划线和连字符组成，且不超过 64 个字符");
        }
        return QString();
    }
    const QString target = QString::fromLatin1(kTargetPrefix) + profileName;
    if (target.size() > CRED_MAX_GENERIC_TARGET_NAME_LENGTH - 1) {
        if (error != nullptr) {
            *error = QStringLiteral("凭据目标名过长");
        }
        return QString();
    }
    return target;
}

bool writeSecret(const QString &targetName, const QByteArray &utf8Secret, QString *error)
{
    if (targetName.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("凭据目标名为空");
        }
        return false;
    }
    if (utf8Secret.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("密钥不能为空");
        }
        return false;
    }
    // Checked up front because a too-large blob fails at the OS boundary with an
    // error code that says nothing about the real cause.
    if (static_cast<DWORD>(utf8Secret.size()) > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        if (error != nullptr) {
            *error = QStringLiteral("密钥长度 %1 字节超过凭据管理器上限 %2 字节")
                         .arg(utf8Secret.size())
                         .arg(CRED_MAX_CREDENTIAL_BLOB_SIZE);
        }
        return false;
    }

    std::wstring target = toWide(targetName);
    std::wstring user = toWide(QString::fromLatin1("image-client"));

    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.UserName = user.data();
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.CredentialBlobSize = static_cast<DWORD>(utf8Secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(utf8Secret.data()));
    GetSystemTimeAsFileTime(reinterpret_cast<FILETIME *>(&credential.LastWritten));

    if (!CredWriteW(&credential, 0)) {
        const DWORD code = GetLastError();
        if (error != nullptr) {
            *error = winError(L"CredWrite", code);
        }
        return false;
    }
    return true;
}

QByteArray readSecret(const QString &targetName, QString *error, bool *absent)
{
    if (absent != nullptr) {
        *absent = false;
    }
    if (targetName.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("凭据目标名为空");
        }
        return QByteArray();
    }

    const std::wstring target = toWide(targetName);
    PCREDENTIALW stored = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &stored)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) {
            if (absent != nullptr) {
                *absent = true;
            }
            return QByteArray();
        }
        if (error != nullptr) {
            *error = winError(L"CredRead", code);
        }
        return QByteArray();
    }

    QByteArray secret(reinterpret_cast<const char *>(stored->CredentialBlob), int(stored->CredentialBlobSize));
    CredFree(stored);
    if (secret.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("凭据 %1 里没有内容").arg(targetName);
        }
    }
    return secret;
}

bool eraseSecret(const QString &targetName, QString *error, bool *existed)
{
    if (existed != nullptr) {
        *existed = true;
    }
    if (targetName.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("凭据目标名为空");
        }
        return false;
    }

    const std::wstring target = toWide(targetName);
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        return true;
    }
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) {
        if (existed != nullptr) {
            *existed = false;
        }
        return true;  // already gone is gone
    }
    if (error != nullptr) {
        *error = winError(L"CredDelete", code);
    }
    return false;
}

QStringList listTargetNames(QString *error)
{
    const std::wstring filter = toWide(QString::fromLatin1(kTargetPrefix) + QStringLiteral("*"));
    PCREDENTIALW *records = nullptr;
    DWORD count = 0;
    if (!CredEnumerateW(filter.c_str(), 0, &count, &records)) {
        const DWORD code = GetLastError();
        if (code != ERROR_NOT_FOUND && error != nullptr) {
            *error = winError(L"CredEnumerate", code);
        }
        return QStringList();
    }

    QStringList targets;
    for (DWORD i = 0; i < count; ++i) {
        if (records[i] != nullptr) {
            targets.append(QString::fromWCharArray(records[i]->TargetName));
        }
    }
    CredFree(records);
    targets.sort();
    return targets;
}

}  // namespace oic::secret
