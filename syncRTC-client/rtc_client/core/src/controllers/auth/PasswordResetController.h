#ifndef PASSWORDRESETCONTROLLER_H
#define PASSWORDRESETCONTROLLER_H

#include <QObject>

class PasswordResetController : public QObject
{
    Q_OBJECT

public:
    explicit PasswordResetController(QObject *parent = nullptr);
};

#endif // PASSWORDRESETCONTROLLER_H
