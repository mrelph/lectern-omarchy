#include "backend.h"
#include "presentationidle.h"
#include "mediaimport.h"
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QUuid>

#include <QFile>
#include <QtDBus>
#include <QSettings>
#include <QDesktopServices>
#include <QDir>
#include <QClipboard>
#include <QMimeData>
#include <QTemporaryDir>
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
#include <QAccessibilityHints>
#endif
#include <QFileInfo>
#include <QGuiApplication>
#include <QLockFile>
#include <QSaveFile>
#include <QScreen>
#include <QQuickTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextDocument>
#include <QStringDecoder>
#include <QStyleHints>

Backend::Backend(QObject *parent) : QObject(parent)
{
    load("# " + tr("Untitled deck") + "\n\n---\n\n## " + tr("Second slide") + "\n", {});
    connect(&m_portal, &Portal::failed, this, &Backend::setStatus);
    connect(&m_portal, &Portal::selected, this, [this](const QString &action, const QUrl &url) {
        const QString path = url.toLocalFile();
        if (action == "media") {
            importMedia(url);
        } else if (action == "folder") {
            QSettings settings("Lectern", "lectern-omarchy");
            auto folders = settings.value("folders").toStringList();
            if (!folders.contains(path)) folders.append(path);
            settings.setValue("folders", folders);
            emit libraryChanged();
        } else if (action == "open") {
            open(path);
        } else if (action == "png") {
            QString error;
            const auto stem = m_path.isEmpty() ? QString("deck") : QFileInfo(m_path).completeBaseName();
            const auto base = QDir(path).filePath(stem + "-slides");
            QString output = base;
            for (int suffix = 2; QFileInfo::exists(output); ++suffix)
                output = base + "-" + QString::number(suffix);
            if (m_deck.exportPng(output, directory(), qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark, &error))
                setStatus(tr("Exported slides to %1").arg(output));
            else
                setStatus(error);
        } else if (action == "pdf" || action == "pdf-notes") {
            QString error;
            if (m_path == path || !path.endsWith(".pdf", Qt::CaseInsensitive))
                setStatus(tr("Choose a PDF filename ending in .pdf"));
            else if (m_deck.exportPdf(path, directory(),
                                      qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark,
                                      &error, action == "pdf-notes"))
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
    m_hasBom = false;
    QSettings("Lectern", "lectern-omarchy").remove("activeDeck");
    load("# " + tr("Untitled deck") + "\n\n---\n\n## " + tr("Second slide") + "\n", {});
}

void Backend::sample()
{
    open(":/example.md");
}

bool Backend::read(const QString &path, QString *source, QString *error, bool *hasBom)
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
    if (hasBom)
        *hasBom = bytes.startsWith("\xef\xbb\xbf");
    return true;
}

bool Backend::open(const QString &path)
{
    QString source, error;
    if (!read(path, &source, &error, &m_hasBom)) {
        setStatus(error);
        return false;
    }
    load(source, path.startsWith(":/") ? QString() : QFileInfo(path).absoluteFilePath());
    remember();
    return true;
}

void Backend::load(const QString &source, const QString &path, int index)
{
    releaseImports();
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
    const QString extension = action == "pdf-notes" ? "-notes.pdf" : action == "pdf" ? ".pdf" : action == "png" ? "-slides" : ".md";
    m_portal.choose(action, stem + extension);
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
    QLockFile lock(QFileInfo(path).absoluteFilePath() + ".lectern.lock");
    if (!lock.tryLock()) {
        setStatus(tr("Another Lectern operation is writing this deck."));
        return;
    }
    if (checkConflict) {
        QString disk, error;
        bool hasBom;
        if (!read(m_path, &disk, &error, &hasBom) || disk != m_saved || hasBom != m_hasBom) {
            setStatus(tr("The file changed on disk. Use Save As to keep your edits."));
            return;
        }
    }
    QSaveFile file(path);
    const auto bytes = (m_hasBom ? QByteArray("\xef\xbb\xbf") : QByteArray()) + m_source.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setStatus(file.errorString());
        return;
    }
    if (path != m_path) releaseImports();
    m_saved = m_source;
    m_path = path;
    syncImports();
    remember();
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
    bool hasBom;
    if (!read(m_path, &disk, &error, &hasBom)) {
        setStatus(tr("Could not reload the deck. %1").arg(error));
        return;
    }
    if (disk == m_saved && m_hasBom == hasBom)
        return;
    if (dirty()) {
        emit externalConflict();
        setStatus(tr("The file changed on disk. Use Save As to keep your edits."));
        return;
    }
    m_hasBom = hasBom;
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
    syncImports();
    m_index = 0;
    followCursor(position);
    emit changed();
}

void Backend::setNotesPending(bool pending)
{
    if (m_notesPending != pending) {
        m_notesPending = pending;
        emit changed();
    }
}

bool Backend::editNotes(int index, const QString &notes, QQuickTextDocument *document, int position)
{
    if (!document || !document->textDocument() || index < 0 || index >= count())
        return false;
    const QString source = QString(m_source).replace("\r\n", "\n");
    const auto updated = Deck::replaceNotes(source, index, notes);
    if (!updated) {
        setStatus(tr("Notes cannot change the slide structure."));
        return false;
    }
    if (*updated == source)
        return true;

    const auto &slide = m_deck.slides[index];
    const int start = slide.notesStart < 0 ? slide.end : slide.notesStart;
    const int length = updated->size() - source.size() + slide.end - start;
    QTextCursor cursor(document->textDocument());
    cursor.setPosition(start);
    cursor.setPosition(slide.end, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    if (length)
        cursor.insertText(updated->mid(start, length));
    else
        cursor.removeSelectedText();
    cursor.endEditBlock();

    const int selected = m_index;
    edit(*updated, position);
    if (m_index != selected)
        select(selected);
    return true;
}

void Backend::setAppearance(const QString &key, const QString &value, QQuickTextDocument *document)
{
    if (!document || !document->textDocument() ||
        !QStringList{"theme", "font", "code-font", "font-size"}.contains(key) || value.contains('\n'))
        return;
    auto *doc = document->textDocument();
    QTextCursor cursor(doc);
    QString encoded = value;
    if (key == "font" || key == "code-font") {
        encoded.replace("\\", "\\\\").replace("\"", "\\\"");
        encoded = '"' + encoded + '"';
    }
    QString line = key + ": " + encoded;
    if (!m_deck.headerLines) {
        line = "---\n" + line + "\n---\n";
    } else {
        auto block = doc->findBlockByNumber(m_deck.headerLines - 1);
        cursor.setPosition(block.position());
        line += '\n';
        // The parser uses the last occurrence of each field.
        for (block = block.previous(); block.blockNumber() > 0; block = block.previous()) {
            const QString text = block.text();
            if (text.section(':', 0, 0).trimmed().compare(key, Qt::CaseInsensitive) == 0 && text.contains(':')) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + text.size(), QTextCursor::KeepAnchor);
                line.chop(1);
                break;
            }
        }
    }
    const int slide = m_index;
    cursor.insertText(line);
    edit(doc->toPlainText(), cursor.position());
    select(slide);
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

QString Backend::prepareAudienceWindow(QWindow *audience, QWindow *presenter) const
{
    for (auto *screen : qGuiApp->screens()) {
        if (screen == presenter->screen())
            continue;
        // Recreate the native surface so the compositor sees the requested
        // output on the first fullscreen request of each presentation.
        audience->destroy();
        audience->setWindowState(Qt::WindowNoState);
        audience->setScreen(screen);
        audience->setGeometry(screen->geometry());
        return screen->name();
    }
    return {};
}

void Backend::paint(QPainter &painter, bool dark, int index, const QString &previewTheme) const
{
    if (index >= count())
        return;
    Slide slide = m_deck.slides[index < 0 ? m_index : index];
    if (!previewTheme.isEmpty()) slide.theme = previewTheme;
    slide.paint(painter, {m_deck.width, 720}, dark,
                                directory(), m_deck.font, m_deck.codeFont, m_deck.fontSize, m_deck.theme);
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

QString Backend::transition() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    if (m_deck.transition == "slide" &&
        qGuiApp->styleHints()->accessibility()->motionPreference() == Qt::MotionPreference::ReducedMotion)
        return "fade";
#endif
    return m_deck.transition;
}

bool Backend::keepAwake() const
{
    return QSettings("Lectern", "lectern-omarchy").value("keepAwake", true).toBool();
}

void Backend::setKeepAwake(bool enabled)
{
    QSettings("Lectern", "lectern-omarchy").setValue("keepAwake", enabled);
    presentationActive(m_presenting);
    emit changed();
}

void Backend::presentationActive(bool active, QQuickItem *stage)
{
    if (stage || !active) m_presentationStage = stage;
    m_surfaceInhibitor.reset();
    m_presenting = active;
    const int generation = ++m_inhibitRequest;
    m_inhibitor = {};
    if (!active || !keepAwake())
        return;
    m_surfaceInhibitor = inhibitPresentationIdle(m_presentationStage);
    auto request = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "Inhibit");
    request << "idle" << "Lectern" << tr("Lectern is presenting") << "block";
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_inhibitRequest) return;
        if (reply.isError()) setStatus(tr("Could not keep the screen awake: %1").arg(reply.error().message()));
        else m_inhibitor = reply.value();
    });
}

void Backend::remember()
{
    if (m_path.isEmpty()) return;
    QSettings settings("Lectern", "lectern-omarchy");
    settings.setValue("activeDeck", m_path);
    auto paths = settings.value("recent").toStringList();
    paths.removeAll(m_path);
    paths.prepend(m_path);
    settings.setValue("recent", paths.mid(0, 20));
    emit libraryChanged();
}

QVariantList Backend::library() const
{
    QSettings settings("Lectern", "lectern-omarchy");
    QVariantList rows;
    const QStringList folders = settings.value("folders").toStringList();
    const auto append = [&](const QString &group, const QString &path, bool folder) {
        const QFileInfo info(path);
        QString name = info.fileName();
        if (!folder) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                const auto title = Deck::parse(QString::fromUtf8(file.read(8192))).title;
                if (!title.isEmpty()) name = title;
            }
        }
        rows.append(QVariantMap{{"group", group}, {"path", path}, {"name", name}, {"folder", folder}});
    };
    for (const auto &path : settings.value("recent").toStringList())
        if (QFileInfo(path).isFile() && !folders.contains(QFileInfo(path).absolutePath())) append(tr("Recent"), path, false);
    for (const auto &path : folders) {
        append(path, path, true);
        const QDir directory(path);
        for (const auto &entry : directory.entryInfoList({"*.md", "*.markdown"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
            append(path, entry.absoluteFilePath(), false);
    }
    return rows;
}

void Backend::forgetFolder(const QString &path)
{
    QSettings settings("Lectern", "lectern-omarchy");
    auto folders = settings.value("folders").toStringList();
    folders.removeAll(path);
    settings.setValue("folders", folders);
    emit libraryChanged();
}

void Backend::reveal() const
{
    if (m_path.isEmpty()) return;
    auto request = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
        "org.freedesktop.FileManager1", "ShowItems");
    request << QStringList{QUrl::fromLocalFile(m_path).toString()} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request));
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, folder = directory()] {
        if (watcher->isError()) QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        watcher->deleteLater();
    });
}

int Backend::replaceSelection(QQuickTextDocument *document, int start, int end, const QString &with)
{
    if (!document || !document->textDocument()) return start;
    QTextCursor cursor(document->textDocument());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(with);
    edit(document->textDocument()->toPlainText(), cursor.position());
    return cursor.position();
}

namespace {
QByteArray fileDigest(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return hash.result();
}
bool mentionedOnDisk(const QString &directory, const QString &name)
{
    const auto bytes = name.toUtf8();
    for (const auto &deck : QDir(directory).entryInfoList({"*.md", "*.markdown"}, QDir::Files)) {
        QFile file(deck.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) return true;
        QByteArray tail;
        while (!file.atEnd()) {
            const auto chunk = file.read(65536);
            if (chunk.isEmpty() && file.error() != QFile::NoError) return true;
            tail += chunk;
            if (tail.contains(bytes)) return true;
            tail = tail.right(bytes.size()-1);
        }
    }
    return false;
}

}

Backend::~Backend() { releaseImports(); }

void Backend::importMedia(const QUrl &url)
{
    if (m_path.isEmpty()) {
        setStatus(tr("Save the deck before adding media."));
        return;
    }
    if (!url.isLocalFile()) {
        setStatus(tr("Choose a local image or video."));
        return;
    }
    try {
        QString created, kind;
        const auto relative = MediaImport::importMedia(m_path, url.toLocalFile(), created, kind);
        if (!created.isEmpty()) {
            // A new insertion after Undo starts a fresh history for this pathname.
            for (auto it = m_imports.begin(); it != m_imports.end();) {
                if (it->path == created) {
                    if (!it->held.isEmpty()) QFile::moveToTrash(it->held);
                    it = m_imports.erase(it);
                } else ++it;
            }
            m_imports.append({created, relative, {}, fileDigest(created)});
        }
        emit insertMediaLink("![" + QFileInfo(relative).completeBaseName() + "](" + relative + ")");
    } catch (const QString &error) {
        setStatus(error);
    }
}

void Backend::syncImports()
{
    for (auto &asset : m_imports) {
        const auto name = QFileInfo(asset.path).fileName();
        const bool referenced = m_source.contains(name) || m_saved.contains(name);
        if (referenced && !asset.held.isEmpty()) {
            if (!QFile::rename(asset.held, asset.path)) {
                setStatus(tr("Could not restore media from %1").arg(asset.held));
                continue;
            }
            asset.held.clear();
        } else if (!referenced && asset.held.isEmpty() && !asset.digest.isEmpty() &&
                   !mentionedOnDisk(directory(), name) && fileDigest(asset.path) == asset.digest) {
            const auto store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/held-media";
            const auto held = QDir(store).filePath(QUuid::createUuid().toString(QUuid::Id128) + "-" + QFileInfo(asset.path).fileName());
            if (QDir().mkpath(store) && QFile::rename(asset.path, held))
                asset.held = held;
        }
    }
}

void Backend::releaseImports()
{
    for (const auto &asset : std::as_const(m_imports))
        if (!asset.held.isEmpty()) QFile::moveToTrash(asset.held);
    m_imports.clear();
}

QVariantList Backend::videos() const
{
    QVariantList result;
    if (!m_deck.slides[m_index].body.contains(QRegularExpression(R"(\.(mp4|mov|m4v)\b)", QRegularExpression::CaseInsensitiveOption)))
        return result;
    QImage scratch(1, 1, QImage::Format_RGB32);
    QPainter painter(&scratch);
    m_deck.slides[m_index].paint(painter, {m_deck.width, 720}, false, directory(), m_deck.font,
        m_deck.codeFont, m_deck.fontSize, m_deck.theme, nullptr, &result);
    return result;
}

bool Backend::pasteMedia()
{
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return false;
    if (mime->hasUrls()) {
        for (const auto &url : mime->urls()) importMedia(url);
        return true;
    }
    if (!mime->hasImage()) return false;
    QTemporaryDir temp;
    const auto file = temp.filePath("clipboard.png");
    if (!temp.isValid() || !QGuiApplication::clipboard()->image().save(file))
        setStatus(tr("Could not read the clipboard image"));
    else
        importMedia(QUrl::fromLocalFile(file));
    return true;
}

void Backend::moveSlide(int direction, QQuickTextDocument *document)
{
    if (!document || !document->textDocument()) return;
    const int target = m_index + direction;
    const QString source = document->textDocument()->toPlainText();
    const QString updated = m_deck.moveSlide(source, m_index, target);
    if (updated == source) return;
    replaceSelection(document, 0, source.size(), updated);
    select(target);
}

void Backend::setSlideTheme(const QString &name, QQuickTextDocument *document)
{
    if (name != "deck" && !Deck::themes().contains(name)) return;
    const auto slide = m_deck.slides[m_index];
    const int index = m_index;
    const int start = slide.themeStart < 0 ? slide.start : slide.themeStart;
    QString directive;
    if (name != "deck") {
        directive = "<!-- theme: " + name + " -->";
        if (slide.themeStart < 0) directive += "\n";
    }
    replaceSelection(document, start, start + slide.themeLength, directive);
    select(index);
}

void Backend::reloadFromDisk()
{
    const int index = m_index;
    if (open(m_path)) select(index);
}

void Backend::restoreLastDeck()
{
    const QString path = QSettings("Lectern", "lectern-omarchy").value("activeDeck").toString();
    if (!path.isEmpty() && QFileInfo(path).isFile()) open(path);
}
