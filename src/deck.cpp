#include "deck.h"
#include <md4c.h>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QCache>
#include <QProcess>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextList>
#include <QFontMetrics>
#include <QPalette>
#include <QMap>
#include <QUrl>
#include <QTemporaryDir>
#include <filesystem>

namespace {
// Classify top-level CommonMark blocks. Nested quote paragraphs and image
// alt text belong to their parent block and must not count as extra content.
struct Layout {
    QStringList blocks;
    QStringList headings;
    const char *source = nullptr;
    QList<QPair<int,int>> comments;
    bool heading = false;
    int depth = 0;
    int imageDepth = 0;
    int images = 0;
    bool paragraphText = false;
    QByteArray html;
};

QString classifyLayout(Slide &slide)
{
    Layout layout;
    MD_PARSER parser { };
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = [](MD_BLOCKTYPE type, void *detail, void *data) {
        auto &state = *static_cast<Layout *>(data);
        if (state.depth++ != 1)
            return 0;
        state.heading = type == MD_BLOCK_H;
        if (state.heading) state.headings.append(QString());
        QString block = "content";
        if (type == MD_BLOCK_H)
            block = static_cast<MD_BLOCK_H_DETAIL *>(detail)->level == 1 ? "title" : "subtitle";
        if (type == MD_BLOCK_CODE)
            block = "code";
        if (type == MD_BLOCK_QUOTE)
            block = "quote";
        if (type == MD_BLOCK_HTML)
            state.html.clear();
        state.images = 0;
        state.paragraphText = false;
        state.blocks.append(block);
        return 0;
    };
    parser.leave_block = [](MD_BLOCKTYPE type, void *, void *data) {
        auto &state = *static_cast<Layout *>(data);
        if (--state.depth != 1)
            return 0;
        if (type == MD_BLOCK_HTML && state.html.trimmed().startsWith("<!--"))
            state.blocks.removeLast();
        if (type == MD_BLOCK_P && state.images == 1 && !state.paragraphText)
            state.blocks.last() = "image";
        return 0;
    };
    parser.enter_span = [](MD_SPANTYPE type, void *, void *data) {
        auto &state = *static_cast<Layout *>(data);
        if (type == MD_SPAN_IMG) {
            ++state.imageDepth;
            ++state.images;
        } else if (!state.imageDepth)
            state.paragraphText = true;
        return 0;
    };
    parser.leave_span = [](MD_SPANTYPE type, void *, void *data) {
        if (type == MD_SPAN_IMG)
            --static_cast<Layout *>(data)->imageDepth;
        return 0;
    };
    parser.text = [](MD_TEXTTYPE type, const char *text, MD_SIZE length, void *data) {
        auto &state = *static_cast<Layout *>(data);
        if (type == MD_TEXT_HTML) {
            state.html.append(text, length);
            if (QByteArrayView(text, length).trimmed().startsWith("<!--"))
                state.comments.append({int(text-state.source), int(length)});
        } else if (state.heading && !state.headings.isEmpty()) {
            state.headings.last() += QString::fromUtf8(text, length);
        }
        if (!state.imageDepth && type != MD_TEXT_SOFTBR && !QByteArrayView(text, length).trimmed().empty())
            state.paragraphText = true;
        return 0;
    };
    auto bytes = slide.body.toUtf8();
    layout.source = bytes.constData();
    if (md_parse(bytes.constData(), bytes.size(), &parser, &layout) != 0)
        return "content";
    slide.heading = layout.headings.value(0).trimmed();
    slide.subtitle = layout.headings.value(1).trimmed();
    for (auto it = layout.comments.crbegin(); it != layout.comments.crend(); ++it)
        bytes.remove(it->first,it->second);
    if (!layout.comments.isEmpty()) slide.body = QString::fromUtf8(bytes).trimmed();
    if (layout.blocks.isEmpty())
        return "empty";
    if (layout.blocks == QStringList { "title", "subtitle" })
        return "title";
    if (layout.blocks.size() == 1 && layout.blocks.first() != "subtitle")
        return layout.blocks.first();
    return "content";
}

class SlideDocument : public QTextDocument {
public:
    explicit SlideDocument(const QString &directory) : m_directory(directory) {}

protected:
    QVariant loadResource(int type, const QUrl &url) override
    {
        if (type != ImageResource || m_directory.isEmpty() || !url.isRelative() ||
            !url.authority().isEmpty() || QDir::isAbsolutePath(url.path()))
            return {};
        const auto path = QDir(m_directory).filePath(url.path());
        const QString ext = QFileInfo(path).suffix().toLower();
        if (QStringList{"mp4", "m4v", "mov", "heic", "heif", "pdf"}.contains(ext)) {
            static thread_local QCache<QString, QImage> posters(32);
            const QFileInfo file(path);
            if (!file.isFile()) return {};
            const auto key = path + QString::number(file.lastModified().toMSecsSinceEpoch()) + QString::number(file.size());
            if (const auto *poster = posters.object(key)) return *poster;
            QProcess decoder;
            if (ext == "pdf") {
                decoder.start("pdftoppm", {"-singlefile", "-f", "1", "-l", "1", "-scale-to", "1160", "-png", path});
            } else {
                decoder.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-protocol_whitelist", "file,pipe",
                "-f", "mov", "-i", path, "-frames:v", "1", "-vf", "scale=1160:570:force_original_aspect_ratio=decrease",
                "-f", "image2pipe", "-vcodec", "png", "pipe:1"});
            }
            if (!decoder.waitForFinished(5000)) { decoder.kill(); decoder.waitForFinished(); }
            auto poster = QImage::fromData(decoder.readAllStandardOutput(), "PNG");
            posters.insert(key, new QImage(poster));
            return poster;
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.width() > 1160 || size.height() > 570)
            reader.setScaledSize(size.scaled(1160, 570, Qt::KeepAspectRatio));
        return reader.read();
    }

private:
    QString m_directory;
};

QString fieldValue(QString value)
{
    value = value.trimmed();
    if (value.startsWith('"') || value.startsWith('\'')) {
        const auto quoted = QRegularExpression(R"re(^("((?:\\.|[^"\\])*)"|'([^']*)')(?:\s*#.*)?$)re").match(value);
        if (quoted.hasMatch()) {
            if (value.startsWith('"'))
                return quoted.captured(2).replace(QRegularExpression(R"re(\\(["\\]))re"), "\\1");
            return quoted.captured(3);
        }
    }
    return value.section(QRegularExpression("\\s+#"), 0, 0).trimmed();
}

bool drawCentered(QPainter &painter, const QString &text, const QRectF &rect, QFont font, int start)
{
    for (int size = start; size >= qMin(start, 18); size -= 2) {
        font.setPixelSize(size);
        painter.setFont(font);
        const int flags = Qt::AlignCenter | Qt::TextWordWrap;
        const auto bounds = painter.boundingRect(rect, flags, text);
        const bool fits = bounds.height() <= rect.height() && bounds.width() <= rect.width();
        if (fits || size <= 19) {
            painter.drawText(rect, flags, text);
            return fits;
        }
    }
    return false;
}
}

QStringList Deck::themes()
{
    return {"auto", "light", "dark", "high-contrast", "warm-paper", "bold-color",
            "minimal-mono", "blocks-light", "blocks-dark"};
}

Deck Deck::parse(const QString &text)
{
    static const QRegularExpression field(R"(^([A-Za-z_][\w-]*)\s*:\s*(.*)$)");
    static const QRegularExpression fence(R"(^ {0,3}(`{3,}|~{3,})(.*)$)");
    static const QRegularExpression note(R"(^notes?:\s*(.*)$)", QRegularExpression::CaseInsensitiveOption);

    Deck deck;
    const auto lines = text.split('\n');
    int first = 0;
    if (lines.value(0).trimmed() == "---") {
        QMap<QString, QString> fields;
        for (int i = 1; i < lines.size(); ++i) {
            const auto line = lines[i].trimmed();
            if (line == "---" || line == "...") {
                if (!fields.isEmpty())
                    first = i + 1;
                break;
            }
            const auto match = field.match(line);
            if (match.hasMatch())
                fields[match.captured(1).toLower()] = fieldValue(match.captured(2));
            else if (fields.isEmpty() && !line.isEmpty() && !line.startsWith('#'))
                break;
        }
        if (first) {
            deck.title = fields.value("title");
            deck.theme = fields.value("theme", "auto").toLower();
            const QString aspect = fields.value("aspect", "16:9").remove(' ');
            if (aspect == "4:3")
                deck.width = 960;
            else if (aspect != "16:9")
                deck.warnings.append(QObject::tr("Unknown aspect %1").arg(aspect));
            deck.transition = fields.value("transition", "fade").toLower();
            if (!QStringList{"none", "slide", "fade"}.contains(deck.transition)) {
                deck.warnings.append(QObject::tr("Unknown transition %1").arg(deck.transition));
                deck.transition = "fade";
            }
            if (!themes().contains(deck.theme)) {
                deck.warnings.append(QObject::tr("Unknown theme %1").arg(deck.theme));
                deck.theme = "auto";
            }
            deck.headerLines = first;
            deck.font = fields.value("font");
            const int size = fields.value("font-size").toInt();
            if (size >= 12 && size <= 96)
                deck.fontSize = size;
            deck.codeFont = fields.value("code-font", fields.value("code_font", fields.value("codefont"))).toLower();
        }
    }
    Slide slide;
    QStringList body, notes;
    bool inNotes = false;
    QString delimiter;
    int offset = 0;
    for (int i = 0; i < first; ++i)
        offset += lines[i].size() + 1;
    slide.start = qMin<qsizetype>(offset, text.size());

    auto finish = [&](int end) {
        slide.end = end;
        while (!body.isEmpty() && body.first().trimmed().isEmpty())
            body.removeFirst();
        slide.body = body.join('\n');
        while (!slide.body.isEmpty() && slide.body.back().isSpace())
            slide.body.chop(1);
        slide.notes = notes.join('\n').trimmed();
        slide.layout = classifyLayout(slide);
        deck.slides.append(slide);
        if (slide.layout == "empty")
            deck.warnings.append(QObject::tr("Slide %1 is empty").arg(deck.slides.size()));
        if (deck.title.isEmpty())
            deck.title = slide.heading;
        body.clear();
        notes.clear();
        inNotes = false;
    };

    for (int i = first; i < lines.size(); ++i) {
        const int lineStart = offset;
        QString line = lines[i];
        offset += line.size() + 1;
        if (line.endsWith('\r'))
            line.chop(1);
        const auto mark = fence.match(line);
        bool fenced = !delimiter.isEmpty();
        if (mark.hasMatch()) {
            const auto run = mark.captured(1);
            const auto tail = mark.captured(2);
            if (fenced && run[0] == delimiter[0] && run.size() >= delimiter.size() && tail.trimmed().isEmpty())
                delimiter.clear();
            else if (!fenced && (run[0] == '~' || !tail.contains('`'))) {
                delimiter = run;
                fenced = true;
            }
        }
        if (!fenced && line.trimmed() == "---") {
            finish(lineStart);
            slide = {};
            slide.start = qMin<qsizetype>(offset, text.size());
            continue;
        }
        const auto noteMatch = note.match(line);
        if (!fenced && !inNotes && noteMatch.hasMatch()) {
            inNotes = true;
            slide.notesStart = lineStart;
            notes.append(noteMatch.captured(1));
        } else if (inNotes) {
            notes.append(line);
        } else {
            static const QRegularExpression overridePattern(R"(<!--\s*theme\s*:\s*([^\s>]+)\s*-->)",
                                                            QRegularExpression::CaseInsensitiveOption);
            const auto match = overridePattern.match(line);
            if (!fenced && match.hasMatch() && slide.theme.isEmpty()) {
                slide.themeStart = lineStart + match.capturedStart();
                slide.themeLength = match.capturedLength();
                const auto name = match.captured(1).toLower();
                if (themes().contains(name))
                    slide.theme = name;
                else
                    deck.warnings.append(QObject::tr("Slide %1 has unknown theme %2").arg(deck.slides.size() + 1).arg(name));
                line.remove(match.capturedStart(), match.capturedLength());
            }
            body.append(line);
        }
    }
    finish(text.size());
    if (!delimiter.isEmpty())
        deck.warnings.append(QObject::tr("A code fence is still open"));
    return deck;
}

std::optional<QString> Deck::replaceNotes(const QString &source, int index, const QString &notes)
{
    const auto deck = parse(source);
    if (index < 0 || index >= deck.slides.size())
        return std::nullopt;
    const auto &slide = deck.slides[index];
    const QString content = QString(notes).replace("\r\n", "\n").replace('\r', '\n').trimmed();
    if (content == QString(slide.notes).replace("\r\n", "\n") && (!content.isEmpty() || slide.notesStart < 0))
        return source;

    if (!content.isEmpty() && parse("Note:\n" + content + "\n\n---\n").slides.size() != 2)
        return std::nullopt;

    const QString newline = source.contains("\r\n") ? "\r\n" : "\n";
    const int start = slide.notesStart >= 0 ? slide.notesStart : slide.end;
    QString replacement;
    if (!content.isEmpty()) {
        if (slide.notesStart < 0 && start > 0) {
            const auto prefix = QStringView(source).first(start);
            if (!prefix.endsWith(newline))
                replacement += newline;
            if (start > slide.start && !prefix.endsWith(newline + newline))
                replacement += newline;
        }
        replacement += "Note:" + newline + QString(content).replace("\n", newline);
        if (slide.end < source.size())
            replacement += newline + newline;
        else if (source.endsWith(newline))
            replacement += newline;
    }

    const auto candidate = source.left(start) + replacement + source.mid(slide.end);
    const auto updated = parse(candidate);
    if (updated.slides.size() != deck.slides.size() ||
        QString(updated.slides[index].notes).replace("\r\n", "\n") != content ||
        updated.slides[index].body != slide.body)
        return std::nullopt;
    return candidate;
}

void Slide::paint(QPainter &painter, QSize canvas, bool dark, const QString &directory,
                  const QString &fontName, const QString &codeFont, int fontSize, const QString &deckTheme, QStringList *warnings, QVariantList *videos) const
{
    QString preset = theme.isEmpty() ? deckTheme : theme;
    if (preset == "auto")
        preset = dark ? "dark" : "light";
    const bool blocks = preset.startsWith("blocks-");
    const bool quote = layout == "quote";
    const bool code = layout == "code";
    struct Colors { QColor paper, ink, accent, title, titleInk; };
    static const QMap<QString, Colors> palettes{
        {"light", {"#fffdf7", "#22242a", "#234f8b", "#1b6366", "#fbf8f1"}},
        {"dark", {"#171b23", "#f5f3ee", "#a5cbff", "#12302f", "#fbf8f1"}},
        {"warm-paper", {"#f6efe2", "#3b2f25", "#8f3f12", "#5a3a22", "#f6efe2"}},
        {"high-contrast", {"black", "white", "#ffd60a", "black", "white"}},
        {"bold-color", {"#1b2a8a", "white", "#ffcf3f", "#c7381a", "white"}},
        {"minimal-mono", {"white", "#111111", "#111111", "white", "#111111"}},
        {"blocks-light", {"#f5f3ef", "black", "#b54400", "#f5f3ef", "black"}},
        {"blocks-dark", {"black", "white", "#ff6200", "black", "white"}}
    };
    const auto colors = palettes.value(preset, palettes.value("light"));
    const auto foreground = colors.ink;
    painter.fillRect(QRect(QPoint(), canvas), colors.paper);
    painter.setPen(foreground);
    if (blocks && quote) {
        painter.fillRect(canvas.width()-200, 440, 40, 40, QColor("#8f0a3e"));
        painter.fillRect(canvas.width()-160, 480, 160, 160, QColor("#ffdd42"));
        painter.fillRect(canvas.width()-240, 640, 80, 80, QColor("#ff6200"));
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QRectF content(80, 70, canvas.width() - 160, canvas.height() - 140);
    if (quote && blocks) content.adjust(0, 0, -120, 0);
    static const QMap<QString, QString> families{{"roboto", "Roboto"}, {"ember", "Ember Modern Text"},
        {"bookerly", "Bookerly"}, {"geist-mono", "Geist Mono"}};
    QFont font("sans-serif");
    auto requestedFamily = families.value(fontName.toLower(), fontName);
    if (requestedFamily.isEmpty() && preset == "warm-paper")
        requestedFamily = QFontDatabase::hasFamily("Bookerly") ? "Bookerly" : "Noto Serif";
    if (requestedFamily.isEmpty() && preset == "minimal-mono") requestedFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    const QStringList candidates{requestedFamily, "Inter", "Noto Sans", "Liberation Sans", "DejaVu Sans"};
    for (const auto &family : candidates) {
        if (!family.isEmpty() && QFontDatabase::hasFamily(family)) {
            font.setFamily(family);
            break;
        }
    }

    QString visible = body;

    if (layout == "title") {
        painter.fillRect(QRect(QPoint(), canvas), colors.title);
        painter.setPen(colors.titleInk);
        if (blocks) {
            painter.fillRect(canvas.width()-180, 599, 121, 121, QColor("#d27de3"));
            painter.fillRect(canvas.width()-59, 539, 59, 59, QColor("#fb6727"));
        }
        font.setBold(true);
        QRectF titleArea = content;
        if (blocks) titleArea.adjust(0, 60, -40, -40);
        else if (!subtitle.isEmpty())
            titleArea.setHeight(content.height() * .8);
        if (!drawCentered(painter, heading, titleArea, font, qRound(86 * fontSize / 36.0)) && warnings)
            warnings->append(QObject::tr("Title does not fit the slide"));
        if (!subtitle.isEmpty()) {
            font.setBold(false);
            drawCentered(painter, subtitle, QRectF(80, blocks ? 145 : 475, content.width(), 100), font, qRound((blocks ? 30 : 42) * fontSize / 36.0));
        }
        return;
    }

    SlideDocument doc(directory);
    doc.setDocumentMargin(0);
    if (quote) {
        painter.setPen(colors.accent);
        QFont mark = font;
        mark.setPixelSize(104);
        painter.setFont(mark);
        painter.drawText(QPointF(content.x(), content.y()+75), QString(QChar(0x201c)));
        content.adjust(20, 72, -20, 0);
    } else if (code) {
        painter.fillRect(content, colors.ink);
        painter.fillRect(QRectF(content.x(),content.y(),6,content.height()), colors.accent);
        content.adjust(30, 28, -30, -28);
    } else if (!blocks && !heading.isEmpty()) {
        painter.fillRect(QRectF(content.x(), content.y()-16, 52, 4), colors.accent);
    }
    font.setPixelSize(quote ? qRound(fontSize * 1.35) : fontSize);
    doc.setDefaultFont(font);
    doc.setMarkdown(visible, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    struct VideoBox { QTextBlock block; int position; QUrl url; QSizeF size; };
    QList<VideoBox> videoBoxes;
    const auto record = [videos](const QUrl &url, QRectF rect) {
        if (videos) videos->append(QVariantMap{{"url", url}, {"left", rect.x()}, {"top", rect.y()},
                                               {"width", rect.width()}, {"height", rect.height()}});
    };
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        if (blocks && block.textList()) {
            auto list = block.textList();
            auto format = list->format();
            if (format.style() == QTextListFormat::ListDisc || format.style() == QTextListFormat::ListCircle) {
                format.setStyle(QTextListFormat::ListSquare);
                list->setFormat(format);
            }
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            auto fragment = it.fragment();
            if (!fragment.charFormat().isImageFormat()) continue;
            auto format = fragment.charFormat().toImageFormat();
            const QUrl url(format.name());
            const auto image = doc.resource(QTextDocument::ImageResource, url).value<QImage>();
            if (image.isNull()) continue;
            const bool video = QStringList{"mp4", "mov", "m4v"}.contains(QFileInfo(url.path()).suffix().toLower());
            const auto file = QUrl::fromLocalFile(QDir(directory).absoluteFilePath(url.path()));
            const auto hint = format.stringProperty(QTextFormat::ImageTitle).toLower();
            static const QRegularExpression percent(R"((\d+(?:\.\d+)?)%)");
            const auto match = percent.match(hint);
            const double fraction = match.hasMatch() ? qBound(.01, match.captured(1).toDouble()/100., 1.) : 1.;
            const bool alone = doc.toPlainText().trimmed() == QString(QChar::ObjectReplacementCharacter);
            if (alone) {
                const bool fill = hint.isEmpty();
                const QSizeF size = QSizeF(image.size()).scaled(canvas.width()*fraction, canvas.height(),
                    fill ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
                const qreal x = hint.contains("left") ? 0 : hint.contains("right") ? canvas.width()-size.width() : (canvas.width()-size.width())/2;
                const QRectF rect(QPointF(x, (canvas.height()-size.height())/2), size);
                painter.save();
                painter.setClipRect(QRect(QPoint(), canvas));
                painter.drawImage(rect, image);
                painter.restore();
                if (video) record(file,rect);
                return;
            }
            const auto size = QSizeF(image.size()).scaled(content.width()*fraction, content.height()*.76, Qt::KeepAspectRatio);
            format.setWidth(size.width()); format.setHeight(size.height());
            QTextCursor cursor(&doc);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position()+fragment.length(), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(format);
            if (block.text().trimmed() == QString(QChar::ObjectReplacementCharacter)) {
                auto paragraph = cursor.blockFormat();
                paragraph.setAlignment(hint.contains("right") ? Qt::AlignRight : hint.contains("left") ? Qt::AlignLeft : Qt::AlignHCenter);
                cursor.setBlockFormat(paragraph);
            }
            if (video) videoBoxes.append({block, fragment.position()-block.position(), file, size});
        }
    }
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (codeFont == "geist-mono" && QFontDatabase::hasFamily("Geist Mono"))
        mono.setFamily("Geist Mono");
    // Qt marks Markdown code spans and blocks with a monospace family.
    QVector<QPair<int, int>> codeRanges;
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            if (warnings && format.isImageFormat()) {
                const auto name = format.toImageFormat().name();
                const QUrl url(name);
                if (!url.isRelative() || !url.authority().isEmpty() || QDir::isAbsolutePath(url.path()))
                    warnings->append(QObject::tr("External media is unavailable: %1").arg(name));
                else if (!QStringList{"png","jpg","jpeg","gif","heic","heif","tif","tiff","bmp","webp","svg","pdf","mp4","mov","m4v"}.contains(QFileInfo(url.path()).suffix().toLower()))
                    warnings->append(QObject::tr("Unsupported media: %1").arg(name));
                else if (!QFileInfo(QDir(directory).filePath(url.path())).isFile())
                    warnings->append(QObject::tr("Missing media: %1").arg(name));
                else if (doc.resource(QTextDocument::ImageResource, url).value<QImage>().isNull())
                    warnings->append(QObject::tr("Could not decode media: %1").arg(name));
            }
            if (format.fontFixedPitch() || format.fontFamilies().toStringList().contains("monospace"))
                codeRanges.append({fragment.position(), fragment.length()});
        }
    }
    for (const auto &range : codeRanges) {
        QTextCursor cursor(&doc);
        cursor.setPosition(range.first);
        cursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setFontFamilies({mono.family()});
        cursor.mergeCharFormat(format);
    }
    doc.setTextWidth(content.width());

    // Measure once at the design size and scale both dimensions for dense slides.
    const auto size = doc.size();
    const qreal scale = qMin(1., qMin(content.width() / qMax(1., size.width()),
                                      content.height() / qMax(1., size.height())));
    if (quote) content.translate(0, qMax(0., (content.height()-size.height()*scale)/2));
    for (const auto &video : videoBoxes) {
        const auto line = video.block.layout()->lineForTextPosition(video.position);
        if (!line.isValid()) continue;
        const auto blockRect = doc.documentLayout()->blockBoundingRect(video.block);
        const QPointF pos = blockRect.topLeft() + QPointF(line.cursorToX(video.position), line.y()+line.ascent()-video.size.height());
        record(video.url, QRectF(content.topLeft()+pos*scale, video.size*scale));
    }
    painter.save();
    if (warnings && scale * fontSize < 16)
        warnings->append(QObject::tr("Content is too dense to read at presentation size"));
    painter.translate(content.topLeft());
    painter.scale(scale, scale);
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette.setColor(QPalette::Text, code ? colors.paper : foreground);
    ctx.palette.setColor(QPalette::Link, colors.accent);
    doc.documentLayout()->draw(&painter, ctx);
    painter.restore();
}

namespace {
bool paintHandout(QPdfWriter &writer, QPainter &painter, const Deck &deck, int index,
                  const QString &directory, bool dark, int &pages)
{
    const auto &slide = deck.slides[index];
    constexpr int margin = 44;
    constexpr int contentWidth = 612 - 2 * margin;
    const qreal scale = qreal(contentWidth) / deck.width;
    painter.save();
    painter.translate(margin, margin + 26);
    painter.scale(scale, scale);
    slide.paint(painter, {deck.width, 720}, dark, directory, deck.font, deck.codeFont, deck.fontSize, deck.theme);
    painter.restore();

    painter.setPen(Qt::black);
    QFont font("sans-serif");
    font.setPixelSize(12);
    auto drawHeading = [&](bool continued) {
        QFont headingFont = font;
        headingFont.setBold(true);
        painter.setFont(headingFont);
        const QString heading = continued ? QObject::tr("Slide %1 notes (continued)").arg(index + 1)
                                         : QObject::tr("Slide %1").arg(index + 1);
        painter.drawText(QRectF(margin, margin, contentWidth, 20), Qt::AlignLeft | Qt::AlignVCenter, heading);
        painter.setFont(font);
    };
    drawHeading(false);

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    qreal y = margin + 26 + 720 * scale + 26;
    constexpr int bottom = 792 - margin;
    for (const auto &paragraph : slide.notes.split('\n')) {
        if (paragraph.isEmpty()) {
            y += QFontMetricsF(font).height();
            continue;
        }
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(contentWidth);
            if (y + line.height() > bottom) {
                if (!writer.newPage())
                    return false;
                ++pages;
                drawHeading(true);
                y = margin + 26;
            }
            line.draw(&painter, QPointF(margin, y));
            y += line.height();
        }
        layout.endLayout();
    }
    return true;
}
}

bool Deck::exportPdf(const QString &path, const QString &directory, bool dark, QString *error, bool withNotes, int *pages) const
{
    int pageCount = slides.size();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setTitle(title);
        writer.setResolution(72);
        writer.setPageSize(withNotes ? QPageSize(QPageSize::Letter)
                                     : QPageSize(QSizeF(width, 720), QPageSize::Point));
        writer.setPageMargins(QMarginsF());
        QPainter painter(&writer);
        if (!painter.isActive()) {
            *error = QObject::tr("Could not create the PDF");
            return false;
        }
        for (int i = 0; i < slides.size(); ++i) {
            if (i && !writer.newPage()) {
                *error = QObject::tr("Could not add a PDF page");
                return false;
            }
            if (withNotes) {
                if (!paintHandout(writer, painter, *this, i, directory, dark, pageCount)) {
                    *error = QObject::tr("Could not add a PDF page");
                    return false;
                }
            } else {
                slides[i].paint(painter, {width, 720}, dark, directory, font, codeFont, fontSize, theme);
            }
        }
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    if (pages) *pages = pageCount;
    return true;
}

QStringList Deck::validate(const QString &directory) const
{
    QStringList result = warnings;
    QImage scratch(1, 1, QImage::Format_RGB32);
    QPainter painter(&scratch);
    for (int i = 0; i < slides.size(); ++i) {
        QStringList problems;
        slides[i].paint(painter, {width, 720}, false, directory, font, codeFont, fontSize, theme, &problems);
        for (const auto &problem : problems)
            result.append(QObject::tr("Slide %1: %2").arg(i + 1).arg(problem));
    }
    return result;
}

bool Deck::exportPng(const QString &folder, const QString &directory, bool dark, QString *error) const
{
    const QFileInfo target(folder);
    if (target.isSymLink() || (target.exists() && (!target.isDir() || !QDir(folder).isEmpty()))) {
        *error = QObject::tr("Choose a new or empty folder for slide images");
        return false;
    }
    if (!QDir().mkpath(target.absolutePath())) {
        *error = QObject::tr("Could not create the slide image folder");
        return false;
    }
    QTemporaryDir staging(target.absolutePath() + "/.lectern-images-XXXXXX");
    if (!staging.isValid()) {
        *error = staging.errorString();
        return false;
    }
    const int digits = qMax(2, int(QString::number(slides.size()).size()));
    for (int i = 0; i < slides.size(); ++i) {
        QImage image(1920, qRound(1920.0 * 720 / width), QImage::Format_RGB32);
        QPainter painter(&image);
        painter.scale(1920.0 / width, 1920.0 / width);
        slides[i].paint(painter, {width, 720}, dark, directory, font, codeFont, fontSize, theme);
        painter.end();
        const auto name = QString("slide-%1.png").arg(i + 1, digits, 10, QLatin1Char('0'));
        if (!image.save(staging.filePath(name))) {
            *error = QObject::tr("Could not write %1").arg(name);
            return false;
        }
    }
    if (target.exists()) QFile::setPermissions(staging.path(), target.permissions());
    // POSIX rename publishes the whole set and refuses to replace a nonempty directory.
    std::error_code failure;
    std::filesystem::rename(QFile::encodeName(staging.path()).constData(), QFile::encodeName(target.absoluteFilePath()).constData(), failure);
    if (failure) {
        *error = QObject::tr("Could not create the slide image folder");
        return false;
    }
    staging.setAutoRemove(false);
    return true;
}

QString Deck::moveSlide(const QString &text, int from, int to) const
{
    if (from == to || from < 0 || to < 0 || from >= slides.size() || to >= slides.size()) return text;
    const QString nl = text.contains("\r\n") ? "\r\n" : "\n";
    QStringList pieces;
    for (const auto &slide : slides) pieces.append(text.mid(slide.start, slide.end-slide.start));
    pieces.move(from, to);
    QString updated = text.left(slides.first().start);
    for (int i = 0; i < slides.size(); ++i) {
        updated += pieces[i];
        if (i+1 < slides.size()) {
            if (!updated.endsWith('\n')) updated += nl;
            updated += text.mid(slides[i].end, slides[i+1].start-slides[i].end);
        }
    }
    return updated;
}
