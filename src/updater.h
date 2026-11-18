#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)

public:
    Updater(const QString &version, const QString &executable);
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool available() const { return m_available; }
    bool canInstall() const { return m_available && !m_asset.isEmpty() && !m_executable.isEmpty(); }
    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void openRelease();

signals:
    void changed();

private:
    QNetworkReply *download(const QUrl &url, qint64 limit);
    void finish(const QString &status);

    QNetworkAccessManager m_network;
    QString m_version, m_executable, m_status;
    QUrl m_asset, m_release;
    QByteArray m_digest, m_installedDigest;
    qint64 m_size = 0;
    bool m_busy = false;
    bool m_available = false;
};
