#include "deck.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QPalette>
#include <QMap>
#include <QUrl>

namespace {
class SlideDocument : public QTextDocument {
public:
    explicit SlideDocument(const QString &directory) : m_directory(directory) {}

protected:
    QVariant loadResource(int type, const QUrl &url) override
    {
        if (type != ImageResource || m_directory.isEmpty() || !url.isRelative() ||
            !url.authority().isEmpty() || QDir::isAbsolutePath(url.path()))
            return {};
        QImageReader reader(QDir(m_directory).filePath(url.path()));
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

void drawCentered(QPainter &painter, const QString &text, const QRectF &rect, QFont font, int start)
{
    for (int size = start; size >= 18; size -= 2) {
        font.setPixelSize(size);
        painter.setFont(font);
        const int flags = Qt::AlignCenter | Qt::TextWordWrap;
        if (painter.boundingRect(rect, flags, text).height() <= rect.height() || size <= 19) {
            painter.drawText(rect, flags, text);
            break;
        }
    }
}
}

Deck Deck::parse(const QString &text)
{
    static const QRegularExpression field(R"(^([A-Za-z_][\w-]*)\s*:\s*(.*)$)");
    static const QRegularExpression fence(R"(^ {0,3}(`{3,}|~{3,})(.*)$)");
    static const QRegularExpression note(R"(^notes?:\s*(.*)$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression heading(R"(^ {0,3}#{1,6}\s+(.+)$)");

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
            if (!QStringList{"auto", "light", "dark"}.contains(deck.theme)) {
                deck.warnings.append(QObject::tr("Unknown theme %1").arg(deck.theme));
                deck.theme = "auto";
            }
            deck.font = fields.value("font").toLower();
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
    slide.start = offset;

    auto finish = [&] {
        while (!body.isEmpty() && body.first().trimmed().isEmpty())
            body.removeFirst();
        slide.body = body.join('\n');
        while (!slide.body.isEmpty() && slide.body.back().isSpace())
            slide.body.chop(1);
        slide.notes = notes.join('\n').trimmed();
        deck.slides.append(slide);
        if (slide.body.isEmpty())
            deck.warnings.append(QObject::tr("Slide %1 is empty").arg(deck.slides.size()));
        if (deck.title.isEmpty())
            deck.title = slide.heading;
        body.clear();
        notes.clear();
        inNotes = false;
    };

    for (int i = first; i < lines.size(); ++i) {
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
            finish();
            slide = {};
            slide.start = offset;
            continue;
        }
        const auto noteMatch = note.match(line);
        if (!fenced && !inNotes && noteMatch.hasMatch()) {
            inNotes = true;
            notes.append(noteMatch.captured(1));
        } else if (inNotes) {
            notes.append(line);
        } else {
            if (!fenced && slide.heading.isEmpty())
                slide.heading = heading.match(line).captured(1).trimmed();
            body.append(line);
        }
    }
    finish();
    if (!delimiter.isEmpty())
        deck.warnings.append(QObject::tr("A code fence is still open"));
    return deck;
}

void Slide::paint(QPainter &painter, QSize canvas, bool dark, const QString &directory,
                  const QString &fontName, const QString &codeFont) const
{
    const auto foreground = dark ? QColor("#f5f3ee") : QColor("#22242a");
    painter.fillRect(QRect(QPoint(), canvas), dark ? QColor("#171b23") : QColor("#fffdf7"));
    painter.setPen(foreground);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF content(80, 70, canvas.width() - 160, canvas.height() - 140);
    static const QMap<QString, QString> families{{"roboto", "Roboto"}, {"ember", "Ember Modern Text"},
        {"bookerly", "Bookerly"}, {"geist-mono", "Geist Mono"}};
    QFont font("sans-serif");
    const QString requestedFamily = families.value(fontName);
    const QStringList candidates{requestedFamily, "Inter", "Noto Sans", "Liberation Sans", "DejaVu Sans"};
    for (const auto &family : candidates) {
        if (!family.isEmpty() && QFontDatabase::hasFamily(family)) {
            font.setFamily(family);
            break;
        }
    }

    QString visible = body;
    visible.remove(QRegularExpression(R"(<!--[\s\S]*?-->)"));
    static const QRegularExpression titlePattern(R"(^#\s+([^\n]+)(?:\n\s*#{2,6}\s+([^\n]+))?\s*$)");
    const auto titleMatch = titlePattern.match(visible.trimmed());
    if (titleMatch.hasMatch()) {
        font.setBold(true);
        const QString subtitle = titleMatch.captured(2);
        QRectF titleArea = content;
        if (!subtitle.isEmpty())
            titleArea.setHeight(content.height() * .8);
        drawCentered(painter, titleMatch.captured(1), titleArea, font, 86);
        if (!subtitle.isEmpty()) {
            font.setBold(false);
            drawCentered(painter, subtitle, QRectF(80, 475, content.width(), 100), font, 42);
        }
        return;
    }

    SlideDocument doc(directory);
    doc.setDocumentMargin(0);
    font.setPixelSize(36);
    doc.setDefaultFont(font);
    doc.setMarkdown(visible, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (codeFont == "geist-mono" && QFontDatabase::hasFamily("Geist Mono"))
        mono.setFamily("Geist Mono");
    // Qt marks Markdown code spans and blocks with a monospace family.
    QVector<QPair<int, int>> codeRanges;
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
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
    painter.save();
    painter.translate(content.topLeft());
    painter.scale(scale, scale);
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette.setColor(QPalette::Text, foreground);
    ctx.palette.setColor(QPalette::Link, dark ? QColor("#a5cbff") : QColor("#234f8b"));
    doc.documentLayout()->draw(&painter, ctx);
    painter.restore();
}

bool Deck::exportPdf(const QString &path, const QString &directory, bool dark, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setTitle(title);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QSizeF(width, 720), QPageSize::Point));
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
            slides[i].paint(painter, {width, 720}, dark, directory, font, codeFont);
        }
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}
