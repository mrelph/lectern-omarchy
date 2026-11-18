#include "backend.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStyleHints>

Backend::Backend(QObject *parent) : QObject(parent)
{
    newDeck();
    connect(&m_portal, &Portal::failed, this, &Backend::setStatus);
    connect(&m_portal, &Portal::selected, this, [this](const QString &action, const QUrl &url) {
        const QString path = url.toLocalFile();
        if (action == "open") {
            open(path);
        } else if (action == "pdf") {
            QString error;
            if (m_path == path || !path.endsWith(".pdf", Qt::CaseInsensitive))
                setStatus(tr("Choose a PDF filename ending in .pdf"));
            else if (m_deck.exportPdf(path, directory(), darkSlides(m_deck, qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark), &error))
                setStatus(tr("Exported %1").arg(QFileInfo(path).fileName()));
            else
                setStatus(error);
        } else {
            saveTo(path, path == m_path);
        }
    });
    m_diskTimer.setInterval(150);
    m_diskTimer.setSingleShot(true);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_diskTimer.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_diskTimer.start(); });
    connect(&m_diskTimer, &QTimer::timeout, this, &Backend::externalChange);
}

QStringList Backend::headings() const
{
    QStringList result;
    for (const auto &slide : m_deck.slides)
        result.append(slide.heading.isEmpty() ? slide.body.simplified().left(64) : slide.heading);
    return result;
}

void Backend::newDeck()
{
    load("# " + tr("Untitled deck") + "\n\n---\n\n## " + tr("Second slide") + "\n", {});
}

void Backend::sample()
{
    open(":/example.md");
}

bool Backend::read(const QString &path, QString *source, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        *error = file.errorString();
        return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    *source = decoder(bytes);
    if (decoder.hasError()) {
        *error = tr("The deck must be UTF-8 Markdown");
        return false;
    }
    return true;
}

bool Backend::open(const QString &path)
{
    QString source, error;
    if (!read(path, &source, &error)) {
        setStatus(error);
        return false;
    }
    load(source, path.startsWith(":/") ? QString() : QFileInfo(path).absoluteFilePath());
    return true;
}

void Backend::load(const QString &source, const QString &path, int index)
{
    m_path = path;
    m_source = m_saved = source;
    m_status.clear();
    m_deck = Deck::parse(QString(source).replace("\r\n", "\n"));
    m_index = qBound(0, index, count() - 1);
    watch();
    emit sourceLoaded();
    emit changed();
}

void Backend::choose(const QString &action)
{
    const QString stem = m_path.isEmpty() ? QString("deck") : QFileInfo(m_path).completeBaseName();
    m_portal.choose(action, stem + (action == "pdf" ? ".pdf" : ".md"));
}

void Backend::save()
{
    if (m_path.isEmpty())
        choose("save");
    else if (dirty())
        saveTo(m_path, true);
}

void Backend::saveTo(const QString &path, bool checkConflict)
{
    if (!path.endsWith(".md", Qt::CaseInsensitive) && !path.endsWith(".markdown", Qt::CaseInsensitive)) {
        setStatus(tr("Choose a Markdown filename ending in .md or .markdown"));
        return;
    }
    if (checkConflict) {
        QString disk, error;
        if (!read(m_path, &disk, &error) || disk != m_saved) {
            setStatus(tr("The file changed on disk. Use Save As to keep your edits."));
            return;
        }
    }
    QSaveFile file(path);
    const auto bytes = m_source.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setStatus(file.errorString());
        return;
    }
    m_saved = m_source;
    m_path = path;
    watch();
    setStatus(tr("Saved %1").arg(QFileInfo(path).fileName()));
}

void Backend::watch()
{
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    if (!m_path.isEmpty()) {
        m_watcher.addPath(QFileInfo(m_path).absolutePath());
        if (QFileInfo::exists(m_path))
            m_watcher.addPath(m_path);
    }
}

void Backend::externalChange()
{
    if (m_path.isEmpty())
        return;
    watch();
    QString disk, error;
    if (!read(m_path, &disk, &error)) {
        setStatus(tr("Could not reload the deck. %1").arg(error));
        return;
    }
    if (disk == m_saved)
        return;
    if (dirty()) {
        setStatus(tr("The file changed on disk. Use Save As to keep your edits."));
        return;
    }
    load(disk, m_path, m_index);
    setStatus(tr("Reloaded from disk"));
}

void Backend::edit(const QString &source, int position)
{
    if (source == QString(m_source).replace("\r\n", "\n"))
        return;
    // The editor normalizes line endings. Retain the source file's CRLF convention.
    const bool crlf = m_saved.contains("\r\n");
    m_source = source;
    if (crlf)
        m_source.replace("\n", "\r\n");
    m_deck = Deck::parse(QString(source).replace("\r\n", "\n"));
    m_index = 0;
    followCursor(position);
    emit changed();
}

void Backend::followCursor(int position)
{
    int index = 0;
    while (index + 1 < count() && m_deck.slides[index + 1].start <= position)
        ++index;
    if (m_index != index) {
        m_index = index;
        emit changed();
    }
}

int Backend::select(int index)
{
    m_index = qBound(0, index, count() - 1);
    emit changed();
    return m_deck.slides[m_index].start;
}

bool Backend::darkSlides(const Deck &deck, bool systemDark)
{
    return deck.theme == "dark" || (deck.theme == "auto" && systemDark);
}

void Backend::paint(QPainter &painter, bool dark) const
{
    m_deck.slides[m_index].paint(painter, {m_deck.width, 720}, darkSlides(m_deck, dark),
                                directory(), m_deck.font, m_deck.codeFont);
}

QString Backend::directory() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
}

void Backend::setStatus(const QString &message)
{
    m_status = message;
    emit changed();
}
