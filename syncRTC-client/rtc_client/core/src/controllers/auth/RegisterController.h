#ifndef REGISTERCONTROLLER_H
#define REGISTERCONTROLLER_H

#include <QObject>

class RegisterController : public QObject
{
    Q_OBJECT

public:
    explicit RegisterController(QObject *parent = nullptr);
};

#endif // REGISTERCONTROLLER_H
