#include "agentcommands.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QStringDecoder>
#include <QTextStream>

namespace
{
struct Command {
    QString tool;
    QStringList positional, optional, options;
};
const QMap<QString, Command> commands = { { "new", { "create_deck", { "path" }, { }, { "title", "font" } } },
    { "slides", { "read_deck", { "path" }, { }, { } } }, { "info", { "read_deck", { "path" }, { }, { } } },
    { "get", { "read_slide", { "path", "index" }, { }, { } } },
    { "set", { "update_slide", { "path", "index" }, { "markdown" }, { "file" } } },
    { "add", { "add_slide", { "path" }, { "markdown" }, { "file", "at" } } },
    { "remove", { "remove_slide", { "path", "index" }, { }, { } } },
    { "move", { "move_slide", { "path", "from", "to" }, { }, { } } },
    { "font", { "set_deck_font", { "path" }, { "font" }, { "code" } } },
    { "themes", { "list_themes", { }, { }, { } } },
    { "theme", { "set_theme", { "path", "theme" }, { }, { "slide" } } },
    { "notes get", { "get_notes", { "path", "index" }, { }, { } } },
    { "notes set", { "set_notes", { "path", "index" }, { "notes" }, { "file" } } },
    { "media add", { "add_media", { "path", "file" }, { }, { "slide", "hint" } } } };

QString help()
{
    return R"(lectern-omarchy commands (slides are numbered from 1)

  new FILE [--title TITLE] [--font FONT]
  info FILE | slides FILE | get FILE N
  add FILE [MARKDOWN|-] [--at N] [--file INPUT]
  set FILE N [MARKDOWN|-] [--file INPUT]
  remove FILE N | move FILE FROM TO
  notes get FILE N | notes set FILE N [TEXT|-] [--file INPUT]
  themes | theme FILE NAME [--slide N] | font FILE [FONT] [--code FONT]
  media add FILE MEDIA --slide N [--hint "50% right"]
  mcp

Use --json for machine-readable results. Omit Markdown or use - to read stdin.
Use --help for open, export, check and skill installation.)";
}

void put(QJsonObject &args, QString key, const QString &value)
{
    if (key == "at")
        key = "before";
    if (key == "code")
        key = "code_font";
    if (QStringList { "index", "slide", "from", "to", "before" }.contains(key)) {
        bool ok;
        int n = value.toInt(&ok);
        if (!ok)
            throw key + " must be a whole number";
        args[key] = n;
    } else
        args[key] = value;
}

QString readInput(QString path, bool fromFile)
{
    QFile file;
    if (fromFile) {
        file.setFileName(path.startsWith("~/") ? QDir::home().filePath(path.mid(2)) : path);
        if (!file.open(QIODevice::ReadOnly))
            throw file.errorString();
    } else if (!file.open(stdin, QIODevice::ReadOnly))
        throw file.errorString();
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(file.readAll());
    if (file.error() != QFile::NoError)
        throw file.errorString();
    if (utf8.hasError())
        throw QString("Expected UTF-8 text");
    return text;
}

QString human(const QString &command, const QJsonObject &out)
{
    if (command == "get")
        return out["markdown"].toString();
    if (command.startsWith("notes "))
        return out["notes"].toString();
    if (command == "info" || command == "slides") {
        QStringList lines;
        if (command == "info")
            lines << QString("%1 (%2 slides)").arg(out["title"].toString()).arg(out["slideCount"].toInt());
        for (auto value : out["slides"].toArray()) {
            auto s = value.toObject();
            lines << QString("%1  %2  %3%4")
                         .arg(s["index"].toInt(), 3)
                         .arg(s["layout"].toString(), -8)
                         .arg(s["title"].toString("(no heading)"), s["hasNotes"].toBool() ? "  [notes]" : "");
        }
        return lines.join('\n');
    }
    if (command == "themes") {
        QStringList lines;
        for (auto value : out["themes"].toArray()) {
            auto theme = value.toObject();
            lines << theme["name"].toString() + "  " + theme["summary"].toString();
        }
        return lines.join('\n');
    }
    if (command == "font")
        return out["font"].toString() + " (code: " + out["codeFont"].toString() + ")";
    if (command == "theme")
        return out["slide"].toObject()["theme"].toString(out["theme"].toString());
    if (command == "media add")
        return out["asset"].toString();
    return QString("%1 (%2 slides)").arg(out["path"].toString()).arg(out["slideCount"].toInt());
}
}

bool AgentCommands::handles(const QString &command)
{
    return commands.contains(command)
        || QStringList { "notes", "media", "mcp", "help", "version", "--version" }.contains(command);
}

int AgentCommands::run(const QStringList &arguments)
{
    QStringList rest = arguments;
    QString name = rest.takeFirst();
    bool json = rest.contains("--json");
    QTextStream out(stdout);
    try {
        if (name == "mcp")
            return serve();
        if (name == "help") {
            out << help() << Qt::endl;
            return 0;
        }
        if (name.endsWith("version")) {
            QString version = QCoreApplication::applicationVersion();
            out << (json ? QString::fromUtf8(QJsonDocument(QJsonObject { { "version", version } })
                                   .toJson(QJsonDocument::Compact))
                         : version)
                << Qt::endl;
            return 0;
        }
        if (name == "notes" || name == "media") {
            if (!rest.isEmpty())
                name += " " + rest.takeFirst();
        }
        if (!commands.contains(name))
            throw help();
        const auto command = commands.value(name);
        QStringList positional;
        QJsonObject args;
        QString input;
        bool literal = false, fromFile = false;
        for (int i = 0; i < rest.size(); ++i) {
            auto token = rest[i];
            if (!literal && token == "--") {
                literal = true;
                continue;
            }
            if (!literal && token == "--json")
                continue;
            if (!literal && token.startsWith("--")) {
                QString option = token.mid(2);
                if (!command.options.contains(option))
                    throw "Unknown option " + token;
                if (++i >= rest.size())
                    throw token + " needs a value";
                if (option == "file") {
                    input = rest[i];
                    fromFile = true;
                } else
                    put(args, option, rest[i]);
            } else
                positional << token;
        }
        auto keys = command.positional + command.optional;
        if (positional.size() < command.positional.size() || positional.size() > keys.size())
            throw "Invalid arguments for " + name + "\n" + help();
        for (int i = 0; i < positional.size(); ++i)
            put(args, keys[i], positional[i]);
        if (command.tool == "add_media" && !args.contains("slide"))
            throw QString("media add needs --slide N");
        if (keys.contains("markdown") || keys.contains("notes")) {
            QString key = keys.last();
            if (fromFile || !args.contains(key) || args[key].toString() == "-")
                args[key] = readInput(input, fromFile);
        }
        auto result = perform(command.tool, args, false);
        out << (json ? QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))
                     : human(name, result))
            << Qt::endl;
        return 0;
    } catch (const QString &error) {
        if (json)
            out << QJsonDocument(QJsonObject { { "error", error } }).toJson(QJsonDocument::Compact)
                << Qt::endl;
        else
            QTextStream(stderr) << error << Qt::endl;
        return 1;
    }
}
