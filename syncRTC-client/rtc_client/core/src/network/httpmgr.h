#ifndef HTTPMGR_H
#define HTTPMGR_H

#include <QObject>

class HttpMgr : public QObject
{
    Q_OBJECT
public:
    explicit HttpMgr(QObject *parent = nullptr);

signals:
};

#endif // HTTPMGR_H
