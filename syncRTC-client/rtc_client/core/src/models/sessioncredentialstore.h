#ifndef SESSIONCREDENTIALSTORE_H
#define SESSIONCREDENTIALSTORE_H

#include <QString>

class SessionCredentialStore
{
public:
    explicit SessionCredentialStore(const QString &targetName = defaultTargetName());

    bool save(const QString &account, const QString &sessionToken) const;
    bool load(QString *account, QString *sessionToken) const;
    bool clear() const;

    static QString defaultTargetName();

private:
    QString m_targetName;
};

#endif // SESSIONCREDENTIALSTORE_H
