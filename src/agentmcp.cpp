#include "agentcommands.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <iostream>

namespace
{
using Object = QJsonObject;
struct Tool {
    QString name, description;
    QStringList required, optional;
};
const QList<Tool> tools = { { "create_deck", "Create a new Markdown deck and title slide.", { "path" },
                                { "title", "font" } },
    { "read_deck", "Read deck metadata and all slides, including Markdown and speaker notes.", { "path" },
        { } },
    { "read_slide", "Read one slide with its notes.", { "path", "index" }, { } },
    { "add_slide", "Append a slide or insert it before a slide number.", { "path", "markdown" },
        { "before" } },
    { "update_slide", "Replace one slide's Markdown.", { "path", "index", "markdown" }, { } },
    { "remove_slide", "Remove a slide. The deck must retain one slide.", { "path", "index" }, { } },
    { "move_slide", "Move a slide to its new one-based position.", { "path", "from", "to" }, { } },
    { "get_notes", "Read one slide's speaker notes.", { "path", "index" }, { } },
    { "set_notes", "Replace speaker notes. An empty string clears them.", { "path", "index", "notes" }, { } },
    { "set_deck_font", "Set the slide font, code font, or both.", { "path" }, { "font", "code_font" } },
    { "list_themes", "List available theme presets.", { }, { } },
    { "set_theme", "Set the deck or slide theme. Use deck to clear a slide override.", { "path", "theme" },
        { "slide" } },
    { "add_media", "Copy a local image or video into assets and link it before the notes.",
        { "path", "file", "slide" }, { "hint" } },
    { "validate_deck", "Validate slide content, layout and local assets.", { "path" }, { } },
    { "export_pdf", "Export slide pages or speaker-notes pages to a PDF.", { "path", "output" },
        { "theme", "notes" } },
    { "export_png", "Export one PNG per slide into a folder.", { "path", "output_dir" }, { "theme" } },
    { "open_in_app", "Launch the native app at an optional slide in editor or presentation mode.", { "path" },
        { "slide", "present" } } };

QString type(const QString &tool, const QString &key)
{
    if (QStringList { "index", "before", "from", "to", "slide" }.contains(key))
        return "integer";
    if (key == "present" || (key == "notes" && tool == "export_pdf"))
        return "boolean";
    return "string";
}

Object schema(const Tool &tool)
{
    Object props;
    for (const auto &key : tool.required + tool.optional) {
        auto t = type(tool.name, key);
        Object prop { { "type", t } };
        if (t == "integer")
            prop["minimum"] = 1;
        if (key == "path")
            prop["description"] = "Path to a .md deck, relative to the server working directory or absolute.";
        if (key == "font")
            prop["enum"] = QJsonArray { "ember", "roboto", "bookerly", "geist-mono" };
        if (key == "code_font")
            prop["enum"] = QJsonArray { "system", "geist-mono" };
        if (key == "markdown")
            prop["description"] = "One slide with optional Note: section.";
        props[key] = prop;
    }
    return { { "name", tool.name }, { "description", tool.description },
        { "inputSchema",
            Object { { "type", "object" }, { "properties", props },
                { "required", QJsonArray::fromStringList(tool.required) },
                { "additionalProperties", false } } } };
}

void check(const Tool &tool, const Object &args)
{
    for (const auto &key : tool.required)
        if (!args.contains(key))
            throw "Missing argument: " + key;
    for (auto i = args.begin(); i != args.end(); ++i) {
        QString key = i.key();
        if (!tool.required.contains(key) && !tool.optional.contains(key))
            throw "Unknown argument: " + key;
        auto t = type(tool.name, key);
        auto v = i.value();
        if ((t == "boolean" && !v.isBool()) || (t == "string" && !v.isString())
            || (t == "integer" && (!v.isDouble() || v.toInt() < 1)))
            throw "Invalid " + t + " for " + key;
    }
}

Object nativeCommand(const QString &name, const Object &args)
{
    const Object deck = AgentCommands::perform("read_deck", args, false);
    QString path = deck["path"].toString();
    const int count = deck["slideCount"].toInt();
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    if (name == "open_in_app") {
        QStringList cli { "open", path };
        if (args.contains("slide")) {
            if (args["slide"].toInt() > count)
                throw QString("Slide number is out of range");
            cli << "--slide" << QString::number(args["slide"].toInt());
        }
        if (args["present"].toBool())
            cli << "--present";
        process.setArguments(cli);
        process.setStandardInputFile(QProcess::nullDevice());
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        if (!process.startDetached())
            throw process.errorString();
        return { { "path", path },
            { "slide", args.value("slide").isUndefined() ? QJsonValue::Null : args.value("slide") },
            { "present", args["present"].toBool() }, { "app", process.program() } };
    }
    const bool validation = name == "validate_deck";
    const bool png = name == "export_png";
    QStringList cli { validation ? "validate" : "export", path, "--json" };
    if (!validation) {
        QString raw = args[png ? "output_dir" : "output"].toString().trimmed();
        if (raw.isEmpty() || raw.contains(QChar::Null))
            throw QString("Expected an output path");
        QFileInfo out(raw.startsWith("~/") ? QDir::home().filePath(raw.mid(2)) : raw);
        if (png && (out.exists() ? !out.isDir() : !out.suffix().isEmpty()))
            throw QString("Expected a PNG folder path");
        cli << (png ? "--png" : "--pdf") << out.absoluteFilePath();
        if (args.contains("theme"))
            cli << "--theme" << args["theme"].toString();
        if (args["notes"].toBool())
            cli << "--notes";
    }
    process.setArguments(cli);
    process.start();
    if (!process.waitForFinished(-1))
        throw process.errorString();
    const Object out = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (process.exitStatus() != QProcess::NormalExit
        || (process.exitCode() && !(validation && out.contains("ok"))) || out.isEmpty()) {
        QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        throw error.isEmpty() ? QString("Native command failed") : error;
    }
    if (validation) {
        QJsonArray warnings;
        for (const auto &warning : out["warnings"].toArray())
            warnings.append(warning.isObject() ? warning.toObject()
                                               : Object { { "message", warning }, { "kind", "validation" },
                                                     { "slide", QJsonValue::Null } });
        return { { "path", path }, { "ok", out["ok"] }, { "warnings", warnings } };
    }
    QString output = out["path"].toString();
    if (!png) {
        const bool notes = args["notes"].toBool();
        Object result{{"output", output}, {"slideCount", count}, {"notes", notes}};
        if (out.contains("pages")) result["pages"] = out["pages"];
        else if (!notes) result["pages"] = count;
        return result;
    }
    QJsonArray files;
    for (const auto &file : QDir(output).entryInfoList({ "slide-*.png" }, QDir::Files, QDir::Name))
        files.append(file.absoluteFilePath());
    return { { "output", output }, { "files", files } };
}

Object call(const Tool &tool, const QJsonValue &arguments)
{
    try {
        if (!arguments.isObject() && !arguments.isUndefined())
            throw QString("Expected an arguments object");
        auto args = arguments.toObject();
        check(tool, args);
        const bool native
            = QStringList { "export_png", "export_pdf", "validate_deck", "open_in_app" }.contains(tool.name);
        const Object result
            = native ? nativeCommand(tool.name, args) : AgentCommands::perform(tool.name, args);
        return {
            { "isError", false }, { "structuredContent", result },
            { "content",
                QJsonArray { Object { { "type", "text" },
                    { "text", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)) } } } }
        };
    } catch (const QString &error) {
        return { { "isError", true },
            { "content", QJsonArray { Object { { "type", "text" }, { "text", "Error: " + error } } } } };
    }
}

Object handle(const QByteArray &line)
{
    auto doc = QJsonDocument::fromJson(line);
    const Object request = doc.object();
    auto id = request.value("id");
    Object response { { "jsonrpc", "2.0" }, { "id", id.isUndefined() ? QJsonValue::Null : id } };
    auto error = [&](int code, const QString &message) {
        response["error"] = Object { { "code", code }, { "message", message } };
        return response;
    };
    if (!doc.isObject())
        return error(-32700, "parse error");
    if (id.isUndefined())
        return { };
    if (!request["method"].isString())
        return error(-32600, "invalid request");
    const QString method = request["method"].toString();
    auto params = request["params"].toObject();
    Object result;
    if (method == "initialize") {
        const QStringList versions { "2025-06-18", "2025-03-26", "2024-11-05" };
        auto asked = params["protocolVersion"].toString();
        result = { { "protocolVersion", versions.contains(asked) ? asked : versions.first() },
            { "capabilities", Object { { "tools", Object { { "listChanged", false } } } } },
            { "serverInfo",
                Object {
                    { "name", "lectern-omarchy" }, { "version", QCoreApplication::applicationVersion() } } },
            { "instructions",
                "Use local .md deck paths and one-based slide indices. A --- line outside code splits "
                "slides. Note: starts speaker notes." } };
    } else if (method == "tools/list") {
        QJsonArray list;
        for (const auto &tool : tools)
            list.append(schema(tool));
        result["tools"] = list;
    } else if (method == "tools/call") {
        const QString name = params["name"].toString();
        auto tool = std::find_if(tools.cbegin(), tools.cend(), [&](const Tool &t) { return t.name == name; });
        if (tool == tools.cend())
            return error(-32602, "unknown tool");
        result = call(*tool, params.value("arguments"));
    } else if (method != "ping")
        return error(-32601, "method not found: " + method);
    response["result"] = result;
    return response;
}
}

int AgentCommands::serve()
{
    for (std::string line; std::getline(std::cin, line);) {
        auto bytes = QByteArray::fromStdString(line).trimmed();
        if (bytes.isEmpty())
            continue;
        auto reply = handle(bytes);
        if (!reply.isEmpty())
            std::cout << QJsonDocument(reply).toJson(QJsonDocument::Compact).constData() << std::endl;
    }
    return std::cin.bad() ? 1 : 0;
}
