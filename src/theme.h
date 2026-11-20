#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appearance READ appearance WRITE setAppearance NOTIFY changed)
    Q_PROPERTY(QString interfacePalette READ interfacePalette WRITE setInterfacePalette NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString appearance() const { return m_appearance; }
    QString interfacePalette() const { return m_palette; }
    void setAppearance(const QString &mode);
    void setInterfacePalette(const QString &name);
    QVariantMap colors() const { return m_colors; }
    QString accent() const { return m_colors.value("accent").toString(); }
    QString accentForeground() const;
    int radius() const { return m_radius; }

signals:
    void changed();

private:
    void reload();
    void refreshRounding();
    void updateColors(bool preferenceChanged = false);

    QString m_directory;
    QString m_appearance, m_palette;
    QHash<QString, QString> m_desktop;
    QVariantMap m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
