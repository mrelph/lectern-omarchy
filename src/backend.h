#pragma once
#include "deck.h"
#include "editorprojection.h"
#include "portal.h"
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QQuickTextDocument>
#include <QQuickItem>
#include <memory>
#include <QWindow>
#include <QDBusUnixFileDescriptor>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(EditorProjection *editorProjection READ editorProjection CONSTANT)
    Q_PROPERTY(QString transition READ transition NOTIFY changed)
    Q_PROPERTY(bool keepAwake READ keepAwake WRITE setKeepAwake NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY sourceLoaded)
    Q_PROPERTY(QString slideFont READ slideFont NOTIFY changed)
    Q_PROPERTY(int slideSize READ slideSize NOTIFY changed)
    Q_PROPERTY(QString slideTheme READ slideTheme NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QStringList headings READ headings NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int slideWidth READ slideWidth NOTIFY changed)

public:
    EditorProjection *editorProjection() { return &m_editorProjection; }
    explicit Backend(QObject *parent = nullptr);
    Q_INVOKABLE QVariantList library() const;
    Q_INVOKABLE void forgetFolder(const QString &path);
    Q_INVOKABLE void reveal() const;
    Q_INVOKABLE int replaceSelection(QQuickTextDocument *document, int start, int end, const QString &with);
    ~Backend() override;
    Q_INVOKABLE void importMedia(const QUrl &url);
    Q_INVOKABLE QVariantList videos() const;
    Q_INVOKABLE void moveSlide(int direction, QQuickTextDocument *document);
    Q_INVOKABLE bool pasteMedia();
    Q_INVOKABLE void setSlideTheme(const QString &name, QQuickTextDocument *document);
    Q_INVOKABLE void reloadFromDisk();
    void restoreLastDeck();
    QString transition() const;
    bool keepAwake() const;
    void setKeepAwake(bool enabled);
    Q_INVOKABLE void presentationActive(bool active, QQuickItem *stage = nullptr);
    Q_INVOKABLE QStringList validate() const { return m_deck.validate(directory()); }
    QString source() const { return m_source; }
    QString slideFont() const { return m_deck.font; }
    int slideSize() const { return m_deck.fontSize; }
    QString slideTheme() const { return m_deck.theme; }
    Q_INVOKABLE void setAppearance(const QString &key, const QString &value, QQuickTextDocument *document);
    QString title() const { return m_deck.title; }
    QString notes() const { return m_deck.slides.at(m_index).notes; }
    QString status() const { return m_status; }
    QStringList headings() const;
    bool dirty() const { return m_notesPending || m_source != m_saved; }
    int index() const { return m_index; }
    int count() const { return m_deck.slides.size(); }
    int slideWidth() const { return m_deck.width; }

    Q_INVOKABLE void newDeck();
    Q_INVOKABLE void sample();
    Q_INVOKABLE void choose(const QString &action);
    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE void save();
    Q_INVOKABLE void edit(const QString &source, int position);
    Q_INVOKABLE void setNotesPending(bool pending);
    Q_INVOKABLE bool editNotes(int index, const QString &notes, QQuickTextDocument *document, int position);
    Q_INVOKABLE void followCursor(int position);
    Q_INVOKABLE int select(int index);
    Q_INVOKABLE QString prepareAudienceWindow(QWindow *audience, QWindow *presenter) const;
    Q_INVOKABLE void resetPresentationTimer() { m_presentationTimer.start(); }
    Q_INVOKABLE int presentationSeconds() const
    {
        return m_presentationTimer.isValid() ? m_presentationTimer.elapsed() / 1000 : 0;
    }
    void paint(QPainter &painter, bool dark, int index = -1, const QString &previewTheme = {}) const;
    static bool read(const QString &path, QString *source, QString *error, bool *hasBom = nullptr);

signals:
    void sourceLoaded();
    void changed();
    void externalConflict();
    void libraryChanged();
    void insertMediaLink(const QString &link);

private:
    void load(const QString &source, const QString &path, int index = 0);
    struct ImportedAsset { QString path, relative, held; QByteArray digest; };
    QList<ImportedAsset> m_imports;
    void syncImports();
    void releaseImports();
    void remember();
    void watch();
    void externalChange();
    void saveTo(const QString &path, bool checkConflict);
    QString directory() const;
    void setStatus(const QString &message);

    QPointer<QQuickItem> m_presentationStage;
    std::unique_ptr<QObject> m_surfaceInhibitor;
    bool m_presenting = false;
    int m_inhibitRequest = 0;
    QDBusUnixFileDescriptor m_inhibitor;
    EditorProjection m_editorProjection;
    Deck m_deck;
    Portal m_portal;
    QFileSystemWatcher m_watcher;
    QTimer m_diskTimer;
    QElapsedTimer m_presentationTimer;
    QString m_source, m_saved, m_path, m_status;
    int m_index = 0;
    bool m_notesPending = false;
    bool m_hasBom = false;
};
