#pragma once
#include <QObject>
#include <QUrl>

class Portal : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void choose(const QString &action, const QString &name = {});

signals:
    void selected(const QString &action, const QUrl &url);
    void failed(const QString &message);

private slots:
    void response(uint status, const QVariantMap &results);

private:
    QString m_request;
    QString m_action;
};
