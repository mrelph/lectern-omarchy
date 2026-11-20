#include "agentcommands.h"
#include "backend.h"
#include "slideview.h"
#include "theme.h"
#include "translator.h"
#include "updater.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QLockFile>

int main(int argc, char **argv)
{
    QByteArray command = argc > 1 ? QByteArray(argv[1]) : QByteArray();
    if (AgentCommands::handles(QString::fromUtf8(command))) {
        QCoreApplication app(argc, argv);
        app.setApplicationVersion(LECTERN_VERSION);
        return AgentCommands::run(app.arguments().mid(1));
    }
    if (command == "validate") command = "check";
    const bool headless = command == "check" || command == "export" || command == "skill";
    if (headless || command == "--help" || command == "-h" || command == "--version" || command == "-v") {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        // Omarchy's GTK platform theme requires a display even with an offscreen Qt backend.
        qunsetenv("QT_QPA_PLATFORMTHEME");
    }
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lectern");
    app.setApplicationName("lectern-omarchy");
    app.setApplicationVersion(LECTERN_VERSION);
    app.setDesktopFileName("lectern-omarchy");
    app.setWindowIcon(QIcon::fromTheme("lectern-omarchy"));
    QQuickStyle::setStyle("Material");

    QCommandLineParser parser;
    parser.setApplicationDescription("Markdown slides for Omarchy");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "open, new, info, slides, get, add, set, remove, move, notes, media, font, themes, theme, check, export, mcp or skill install", "[command]");
    parser.addPositionalArgument("deck", "Path to a Markdown deck", "[deck]");
    parser.addPositionalArgument("output", "Destination for PDF export", "[output.pdf]");
    parser.addOptions({{"json", "Print machine-readable output"}, {"dark", "Use dark appearance for theme: auto"},
                       {"notes", "Include speaker notes in PDF export"}});
    parser.addOptions({{"png", "Export images to a new folder", "folder"},
                       {"pdf", "Export slides to a PDF file", "file"},
                       {"theme", "Override the deck theme during export", "theme"},
                       {"slide", "Open at a one-based slide number", "number"},
                       {"present", "Start in presentation mode"}});
    parser.process(app);
    QStringList args = parser.positionalArguments();

    auto fail = [&parser](const QString &error) {
        if (parser.isSet("json"))
            QTextStream(stdout) << QJsonDocument(QJsonObject{{"error", error}}).toJson(QJsonDocument::Compact) << Qt::endl;
        QTextStream(stderr) << error << Qt::endl;
        return 1;
    };
    if (headless) {
        args.removeFirst();
        QJsonObject result;
        if (command == "skill") {
            if (args != QStringList{"install"})
                return fail("Usage: lectern-omarchy skill install [--json]");
            const QString directory = QDir::homePath() + "/.agents/skills/lectern-omarchy";
            const QString path = directory + "/SKILL.md";
            if (QFileInfo::exists(path))
                return fail("Skill already exists at " + path);
            if (!QDir().mkpath(directory) || !QFile::copy(":/SKILL.md", path))
                return fail("Could not install the skill at " + path);
            result.insert("path", path);
        } else {
            const bool exporting = command == "export";
            const bool png = parser.isSet("png");
            if (exporting && (png || parser.isSet("pdf"))) {
                if (png && (parser.isSet("pdf") || parser.isSet("notes")))
                    return fail("Choose one export format. Notes handouts require PDF.");
                args.append(parser.value(png ? "png" : "pdf"));
            }
            if (args.size() != (exporting ? 2 : 1))
                return fail("Expected a Markdown deck path and, for export, an output PDF path");
            QString source, error;
            if (!Backend::read(args[0], &source, &error))
                return fail(error);
            auto deck = Deck::parse(source);
            if (parser.isSet("theme")) {
                deck.theme = parser.value("theme").toLower();
                if (!Deck::themes().contains(deck.theme))
                    return fail("Unknown theme: " + deck.theme);
            }
            if (exporting && png) {
                if (!deck.exportPng(args[1], QFileInfo(args[0]).absolutePath(), parser.isSet("dark"), &error))
                    return fail(error);
                result.insert("path", QFileInfo(args[1]).absoluteFilePath());
            } else if (exporting) {
                if (!args[1].endsWith(".pdf", Qt::CaseInsensitive))
                    return fail("The output path must end in .pdf");
                QLockFile outputLock(QFileInfo(args[1]).absoluteFilePath() + ".lectern.lock");
                if (!outputLock.tryLock()) return fail("The PDF destination is busy or unavailable");
                if (QFileInfo(args[1]).exists())
                    return fail("The output path already exists");
                int pages = 0;
                if (!deck.exportPdf(args[1], QFileInfo(args[0]).absolutePath(),
                                    parser.isSet("dark"), &error, parser.isSet("notes"), &pages))
                    return fail(error);
                result.insert("pages", pages);
                result.insert("path", QFileInfo(args[1]).absoluteFilePath());
            } else {
                QJsonArray slides;
                for (const auto &slide : deck.slides)
                    slides.append(QJsonObject{{"heading", slide.heading}, {"hasNotes", !slide.notes.isEmpty()}});
                result = {{"title", deck.title}, {"slideCount", deck.slides.size()}, {"slides", slides},
                          {"aspect", deck.width == 960 ? "4:3" : "16:9"}};
                if (command == "check") {
                    const auto warnings = deck.validate(QFileInfo(args[0]).absolutePath());
                    result.insert("warnings", QJsonArray::fromStringList(warnings));
                    result.insert("ok", warnings.isEmpty());
                }
            }
        }
        QTextStream out(stdout);
        if (parser.isSet("json"))
            out << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        else if (result.contains("path"))
            out << result.value("path").toString() << Qt::endl;
        else
            out << result.value("title").toString() << " (" << result.value("slideCount").toInt() << " slides)" << Qt::endl;
        if (command == "check") {
            for (const auto &warning : result.value("warnings").toArray())
                QTextStream(stderr) << warning.toString() << Qt::endl;
            return result.value("ok").toBool() ? 0 : 1;
        }
        return 0;
    }

    if (args.value(0) == "open")
        args.removeFirst();
    if (args.size() > 1)
        return fail("Expected one Markdown deck path");

    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Backend backend;
    Updater updater(app.applicationVersion(), app.applicationFilePath());
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    qmlRegisterType<SlideView>("Lectern", 1, 0, "SlideView");
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("updater", &updater);
    if (args.isEmpty()) backend.restoreLastDeck();
    if (!args.isEmpty() && !backend.open(args[0]))
        return fail(backend.status());
    int startSlide = parser.value("slide").toInt();
    if (parser.isSet("slide") && (startSlide < 1 || startSlide > backend.count()))
        return fail("Slide number is out of range");
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = engine.rootObjects().first();
    if (parser.isSet("slide"))
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, startSlide - 1));
    if (parser.isSet("present"))
        QTimer::singleShot(0, window, [window] { QMetaObject::invokeMethod(window, "startPresentation", Q_ARG(QVariant, false)); });
    return app.exec();
}
