#include "decklook.h"
#include "agentcommands.h"
#include "backend.h"
#include "deck.h"
#include "mediaimport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <cerrno>
#include <cstring>

#include <unistd.h>

namespace
{
using Object = QJsonObject;
using Regex = QRegularExpression;

QString expanded(QString path) { return path.startsWith("~/") ? QDir::home().filePath(path.mid(2)) : path; }

QString deckPath(QString path)
{
    QFileInfo file(expanded(path.trimmed()));
    const QStringList extensions{"md", "markdown"};
    if (path.contains(QChar::Null) || !extensions.contains(file.suffix().toLower()) || file.isDir())
        throw QString("Expected a Markdown deck path");
    if (file.exists()) {
        file = QFileInfo(file.canonicalFilePath());
        if (!file.isFile() || !extensions.contains(file.suffix().toLower()))
            throw QString("Expected a regular Markdown deck");
    } else if (file.isSymLink()) {
        throw QString("Deck link has no target");
    }
    return file.absoluteFilePath();
}

QString trimBlank(QString text)
{
    static const Regex edges(R"(^([ \t\r]*\n)+|(?:\r?\n[ \t\r]*)+$)");
    return text.trimmed().isEmpty() ? QString() : text.remove(edges);
}

struct Preset {
    QString name, label, summary;
    bool dark;
};
const QList<Preset> presets = { { "auto", "Auto", "Follow the desktop appearance.", false },
    { "light", "Inkwell Light", "Light Inkwell colors.", false },
    { "dark", "Inkwell Dark", "Dark Inkwell colors.", true },
    { "high-contrast", "High Contrast", "White on black with yellow accents.", true },
    { "warm-paper", "Warm Paper", "Brown ink on cream paper.", false },
    { "bold-color", "Bold Color", "White on blue with orange-red titles.", true },
    { "minimal-mono", "Minimal Mono", "Black on white in Geist Mono.", false },
    { "blocks-light", "Blocks Light", "Off-white with colored corners.", false },
    { "blocks-dark", "Blocks Dark", "Black with colored corners.", true } };

QString checkedFont(QString font)
{
    font = font.toLower();
    if (!QStringList { "ember", "roboto", "bookerly", "geist-mono", "ubuntu" }.contains(font))
        throw QString("Expected ember, roboto, bookerly, geist-mono or ubuntu");
    return font;
}

Object slideInfo(const Slide &s, const QString &text, int index, bool full)
{
    Object out { { "index", index },
        { "title", s.heading.isEmpty() ? QJsonValue::Null : QJsonValue(s.heading) }, { "heading", s.heading },
        { "layout", s.layout } };
    if (!s.theme.isEmpty())
        out["theme"] = s.theme;
    if (full) {
        out["markdown"] = trimBlank(text.mid(s.start, s.end - s.start)).replace("\r\n", "\n");
        out["notes"] = s.notes;
    } else
        out["hasNotes"] = !s.notes.isEmpty();
    return out;
}

Object info(const QString &path, const Deck &deck, const QString &text, bool full)
{
    QJsonArray slides;
    for (const auto &slide : deck.slides)
        slides.append(slideInfo(slide, text, slides.size() + 1, full));
    return { { "path", path }, { "title", deck.title }, { "slideCount", slides.size() }, { "slides", slides },
        { "theme", deck.theme }, { "font", deck.font.isEmpty() ? "ember" : deck.font },
        { "codeFont", deck.codeFont.isEmpty() ? "system" : deck.codeFont }, { "transition", deck.transition },
        { "aspect", deck.width == 960 ? "4:3" : "16:9" } };
}

QString withField(QString source, const QString &key, const QString &value, const QString &nl)
{
    const auto end = Deck::parse(source).slides.first().start;
    const QString line = key + ": " + value;
    if (!end)
        return "---" + nl + line + nl + "---" + nl + nl + source;
    const QString header = source.left(end);
    const auto match
        = Regex("^" + key + R"([ \t]*:[^\r\n]*)", Regex::CaseInsensitiveOption | Regex::MultilineOption)
              .match(header);
    if (match.hasMatch())
        return source.replace(match.capturedStart(), match.capturedLength(), line);
    int closing = header.lastIndexOf('\n', header.endsWith('\n') ? end - 2 : end - 1) + 1;
    return source.insert(closing, line + nl);
}

QString padded(QString piece, const QString &nl)
{
    if (!piece.endsWith('\n'))
        piece += nl;
    if (!piece.endsWith("\n\n") && !piece.endsWith("\n\r\n"))
        piece += nl;
    return piece;
}

QString replacement(const QString &raw, const QString &content, const QString &nl, bool last)
{
    const auto lead = Regex(R"(^(?:[ \t\r]*\n)*)").match(raw).captured();
    const auto tail = Regex(R"((?:\r?\n[ \t\r]*)+$)").match(raw.mid(lead.size())).captured();
    QString piece = lead + content + tail;
    return last ? piece : padded(piece, nl);
}

void save(const QString &path, const QString &text, bool bom, bool create)
{
    const auto bytes = (bom ? QByteArray("\xef\xbb\xbf") : QByteArray()) + text.toUtf8();
    if (create) {
        // Publish the complete new file without replacing a competing creator's file.
        QTemporaryFile file(path + ".XXXXXX");
        if (!file.open() || file.write(bytes) != bytes.size() || !file.flush())
            throw file.errorString();
        if (::link(QFile::encodeName(file.fileName()).constData(), QFile::encodeName(path).constData()) != 0)
            throw QString("Could not create %1: %2").arg(path, QString::fromLocal8Bit(strerror(errno)));
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw file.errorString();
}

} // namespace

QJsonObject AgentCommands::perform(const QString &tool, const QJsonObject &args, bool full)
{
    if (tool == "list_themes") {
        QJsonArray themes;
        for (const auto &p : presets) {
            const bool mono = p.name == "minimal-mono";
            themes.append(Object { { "name", p.name }, { "label", p.label }, { "summary", p.summary },
                { "dark", p.name == "auto" ? QJsonValue::Null : QJsonValue(p.dark) },
                { "font",
                    mono                         ? "geist-mono"
                        : p.name == "warm-paper" ? "bookerly"
                                                 : "ember" },
                { "codeFont", mono || p.name.startsWith("blocks-") ? "geist-mono" : "system" } });
        }
        for (const auto &look : DeckLook::all()) {
            themes.append(Object{{"name",look.name},{"label",look.label},{"summary",look.summary},
                {"dark",look.style == DeckLook::Dusk || look.style == DeckLook::Terminal},
                {"font",look.font},{"codeFont",look.codeFont}});
        }
        return { { "themes", themes } };
    }
    const QString path = deckPath(args["path"].toString());
    const bool create = tool == "create_deck";
    const bool reading = tool.startsWith("read_") || tool == "get_notes";
    QLockFile lock(path + ".lectern.lock");
    if (!reading && !lock.tryLock())
        throw QString("Could not lock deck for writing: ") + path;
    if (create) {
        if (QFileInfo::exists(path))
            throw QString("Deck already exists: ") + path;
        QString title
            = args["title"].toString(QFileInfo(path).completeBaseName()).replace(Regex("[\r\n]+"), " ");
        if (title.isEmpty())
            title = "Untitled deck";
        const QString font = checkedFont(args["font"].toString("ember"));
        QString quoted = QString(title).replace("\\", "\\\\").replace("\"", "\\\"");
        QString text = "---\ntitle: \"" + quoted
            + "\"\ntheme: auto\naspect: \"16:9\"\ntransition: fade\nfont: " + font + "\n---\n\n# " + title
            + "\n";
        save(path, text, false, true);
        return info(path, Deck::parse(text), text, false);
    }

    QString text, error;
    bool bom = false;
    if (!Backend::read(path, &text, &error, &bom))
        throw error;
    const auto deck = Deck::parse(text);
    const int count = deck.slides.size();
    auto number = [&](const QString &key, int max) {
        int n = args[key].toInt();
        if (n < 1 || n > max)
            throw QString("%1 must be a slide number between 1 and %2").arg(key).arg(max);
        return n;
    };
    if (tool == "read_deck")
        return info(path, deck, text, full);
    int index = args.contains("index") ? number("index", count) : 1;
    if (args.contains("slide"))
        index = number("slide", count);
    if (tool == "move_slide")
        index = number("to", count);
    if (tool == "add_slide")
        index = args.contains("before") ? number("before", count + 1) : count + 1;
    const bool empty = count == 1 && trimBlank(text.mid(deck.slides.first().start)).isEmpty();
    if (tool == "add_slide" && empty)
        index = 1;
    const Slide s = deck.slides[qMin(index, count) - 1];
    if (tool == "read_slide")
        return slideInfo(s, text, index, true);
    if (tool == "get_notes")
        return { { "path", path }, { "index", index }, { "slide", index }, { "title", s.heading },
            { "notes", s.notes } };

    QString updated = text, asset, kind, created;
    auto rollback = qScopeGuard([&] {
        if (!created.isEmpty())
            QFile::remove(created);
    });
    const QString nl = text.contains("\r\n") ? "\r\n" : "\n";
    int expected = count;
    if (tool == "set_notes") {
        auto candidate = Deck::replaceNotes(text, index - 1, args["notes"].toString());
        if (!candidate)
            throw QString("The notes would change the slide structure");
        updated = *candidate;
    } else if (tool == "set_deck_font") {
        if (!args.contains("code_font") && !args.contains("font"))
            throw QString("Give a font or a code font");
        if (args.contains("font"))
            updated = withField(updated, "font", checkedFont(args["font"].toString()), nl);
        if (args.contains("code_font")) {
            QString code = args["code_font"].toString().toLower();
            if (code != "system" && code != "geist-mono")
                throw QString("Expected system or geist-mono for code");
            updated = withField(updated, "code-font", code, nl);
        }
    } else if (tool == "set_theme") {
        const QString theme = args["theme"].toString().toLower();
        const bool perSlide = args.contains("slide");
        if (!(perSlide && theme == "deck") && !Deck::themes().contains(theme))
            throw QString("Unknown theme: ") + theme;
        if (!perSlide)
            updated = withField(updated, "theme", theme, nl);
        else {
            const QString raw = text.mid(s.start, s.end - s.start);
            QString body = raw;
            if (s.themeStart >= 0)
                body.remove(s.themeStart - s.start, s.themeLength);
            body = trimBlank(body);
            if (theme != "deck")
                body = "<!-- theme: " + theme + " -->" + (body.isEmpty() ? "" : nl + nl + body);
            updated.replace(s.start, s.end - s.start, replacement(raw, body, nl, index == count));
        }
    } else if (tool == "add_media") {
        const QString hint = args["hint"].toString().simplified().toLower();
        if (args.contains("hint")) {
            for (auto word : hint.split(' ')) {
                bool ok = false;
                double width = word.chopped(1).toDouble(&ok);
                if (!QStringList { "left", "center", "right", "fit" }.contains(word)
                    && !(word.endsWith('%') && ok && width > 0 && width <= 100))
                    throw QString("Invalid media hint");
            }
        }
        asset = MediaImport::importMedia(path, args["file"].toString(), created, kind);
        const QString alt = QFileInfo(asset).completeBaseName().replace('-', ' ').replace('_', ' ');
        const QString link = "![" + alt + "](" + asset + (hint.isEmpty() ? "" : " \"" + hint + "\"") + ")";
        const int end = s.notesStart < 0 ? s.end : s.notesStart;
        QString body = text.mid(s.start, end - s.start);
        if (!body.isEmpty())
            body = padded(body, nl);
        body += link + nl;
        if (end < text.size())
            body += nl;
        updated.replace(s.start, end - s.start, body);
    } else if (tool == "remove_slide") {
        if (count == 1)
            throw QString("A deck needs at least one slide");
        int start = index == 1 ? s.start : deck.slides[index - 2].end;
        int end = index == 1 ? deck.slides[1].start : s.end;
        updated.remove(start, end - start);
        --expected;
    } else if (tool == "move_slide") {
        updated = deck.moveSlide(text, number("from", count) - 1, index - 1);
    } else if (tool == "add_slide" || tool == "update_slide") {
        QString markdown = trimBlank(args["markdown"].toString().replace("\r\n", "\n"));
        if (markdown.isEmpty())
            throw QString("Slide Markdown is empty");
        // A sentinel on either side rules out front matter and detects swallowed separators.
        const QString prefix = "# Start\n---\n";
        const auto checked = Deck::parse(prefix + markdown + "\n---\n# End");
        if (checked.slides.size() != 3 || checked.slides[1].end != prefix.size() + markdown.size() + 1)
            throw QString("Supply one slide with balanced code fences");
        markdown.replace("\n", nl);
        if (tool == "update_slide" || empty) {
            auto raw = text.mid(s.start, s.end - s.start);
            QString piece = replacement(raw, markdown, nl, index == count);
            if (s.start && text[s.start - 1] != '\n')
                piece.prepend(nl);
            updated.replace(s.start, s.end - s.start, piece);
        } else {
            ++expected;
            if (index == count + 1)
                updated = padded(text, nl) + "---" + nl + nl + markdown + nl;
            else
                updated.insert(s.start, (s.start ? nl : "") + markdown + nl + nl + "---" + nl);
        }
    } else
        throw QString("Unknown editing operation");

    const auto after = Deck::parse(updated);
    if (after.slides.size() != expected)
        throw QString("Edit would change the slide structure");
    if (updated != text)
        save(path, updated, bom, false);
    rollback.dismiss();
    if (tool == "set_deck_font")
        return { { "path", path }, { "font", after.font.isEmpty() ? "ember" : after.font },
            { "codeFont", after.codeFont.isEmpty() ? "system" : after.codeFont } };
    Object result { { "path", path }, { "slideCount", expected } };
    if (tool != "remove_slide" && (tool != "set_theme" || args.contains("slide")))
        result["slide"] = slideInfo(after.slides[index - 1], updated, index, tool != "set_theme");
    if (tool == "set_theme")
        result["theme"] = after.theme;
    if (tool == "set_notes")
        result["notes"] = after.slides[index - 1].notes;
    if (tool == "add_media") {
        result["asset"] = asset;
        result["kind"] = kind;
    }
    return result;
}
