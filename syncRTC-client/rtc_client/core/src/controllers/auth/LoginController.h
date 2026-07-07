#ifndef LOGINCONTROLLER_H
#define LOGINCONTROLLER_H

#include <QObject>

class LoginController : public QObject
{
    Q_OBJECT

public:
    explicit LoginController(QObject *parent = nullptr);
};

#endif // LOGINCONTROLLER_H
