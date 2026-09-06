#include "sessioncredentialstore.h"

#include <QByteArray>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>

#include <string>
#endif

namespace {
constexpr auto kDefaultTargetName = "SyncRTC/rtc_client/last_session";

#ifdef Q_OS_WIN
std::wstring toWideString(const QString &value)
{
    return value.toStdWString();
}
#endif
}

SessionCredentialStore::SessionCredentialStore(const QString &targetName)
    : m_targetName(targetName)
{
}

bool SessionCredentialStore::save(const QString &account, const QString &sessionToken) const
{
    if (account.isEmpty() || sessionToken.isEmpty()) {
        return false;
    }

#ifdef Q_OS_WIN
    const QByteArray tokenBytes = sessionToken.toUtf8();
    if (tokenBytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        qWarning() << "Session token is too large for Credential Manager";
        return false;
    }

    const std::wstring targetName = toWideString(m_targetName);
    const std::wstring userName = toWideString(account);

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(targetName.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(tokenBytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(tokenBytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t *>(userName.c_str());

    if (!CredWriteW(&credential, 0)) {
        qWarning() << "Failed to save remembered login session:" << GetLastError();
        return false;
    }

    return true;
#else
    Q_UNUSED(account)
    Q_UNUSED(sessionToken)
    return false;
#endif
}

bool SessionCredentialStore::load(QString *account, QString *sessionToken) const
{
    if (!account || !sessionToken) {
        return false;
    }

    account->clear();
    sessionToken->clear();

#ifdef Q_OS_WIN
    const std::wstring targetName = toWideString(m_targetName);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND) {
            qWarning() << "Failed to read remembered login session:" << error;
        }
        return false;
    }

    const QString storedAccount = credential->UserName
        ? QString::fromWCharArray(credential->UserName)
        : QString();
    const QByteArray tokenBytes(reinterpret_cast<const char *>(credential->CredentialBlob),
                                static_cast<int>(credential->CredentialBlobSize));
    CredFree(credential);

    const QString storedToken = QString::fromUtf8(tokenBytes);
    if (storedAccount.isEmpty() || storedToken.isEmpty()) {
        return false;
    }

    *account = storedAccount;
    *sessionToken = storedToken;
    return true;
#else
    return false;
#endif
}

bool SessionCredentialStore::clear() const
{
#ifdef Q_OS_WIN
    const std::wstring targetName = toWideString(m_targetName);
    if (!CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0)) {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND) {
            qWarning() << "Failed to clear remembered login session:" << error;
            return false;
        }
    }
#endif

    return true;
}

QString SessionCredentialStore::defaultTargetName()
{
    return QString::fromLatin1(kDefaultTargetName);
}
