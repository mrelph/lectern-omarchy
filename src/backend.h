#pragma once
#include "deck.h"
#include "portal.h"
#include <QFileSystemWatcher>
#include <QTimer>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY sourceLoaded)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QStringList headings READ headings NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int slideWidth READ slideWidth NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr);
    QString source() const { return m_source; }
    QString title() const { return m_deck.title; }
    QString notes() const { return m_deck.slides.at(m_index).notes; }
    QString status() const { return m_status; }
    QStringList headings() const;
    bool dirty() const { return m_source != m_saved; }
    int index() const { return m_index; }
    int count() const { return m_deck.slides.size(); }
    int slideWidth() const { return m_deck.width; }

    Q_INVOKABLE void newDeck();
    Q_INVOKABLE void sample();
    Q_INVOKABLE void choose(const QString &action);
    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE void save();
    Q_INVOKABLE void edit(const QString &source, int position);
    Q_INVOKABLE void followCursor(int position);
    Q_INVOKABLE int select(int index);
    void paint(QPainter &painter, bool dark) const;
    static bool read(const QString &path, QString *source, QString *error);
    static bool darkSlides(const Deck &deck, bool systemDark);

signals:
    void sourceLoaded();
    void changed();

private:
    void load(const QString &source, const QString &path, int index = 0);
    void watch();
    void externalChange();
    void saveTo(const QString &path, bool checkConflict);
    QString directory() const;
    void setStatus(const QString &message);

    Deck m_deck;
    Portal m_portal;
    QFileSystemWatcher m_watcher;
    QTimer m_diskTimer;
    QString m_source, m_saved, m_path, m_status;
    int m_index = 0;
};
