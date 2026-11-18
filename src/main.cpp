#include "backend.h"
#include "slideview.h"
#include "theme.h"
#include "translator.h"

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

int main(int argc, char **argv)
{
    const QByteArray command = argc > 1 ? QByteArray(argv[1]) : QByteArray();
    const bool headless = command == "info" || command == "check" || command == "export" || command == "skill";
    if (headless || command == "--help" || command == "-h" || command == "--version" || command == "-v")
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName("lectern-omarchy");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("lectern-omarchy");
    app.setWindowIcon(QIcon::fromTheme("lectern-omarchy"));
    QQuickStyle::setStyle("Material");

    QCommandLineParser parser;
    parser.setApplicationDescription("Markdown slides for Omarchy");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "open, info, check, export or skill install", "[command]");
    parser.addPositionalArgument("deck", "Path to a Markdown deck", "[deck]");
    parser.addPositionalArgument("output", "Destination for PDF export", "[output.pdf]");
    parser.addOptions({{"json", "Print machine-readable output"}, {"dark", "Use dark appearance for theme: auto"}});
    parser.process(app);
    QStringList args = parser.positionalArguments();

    auto fail = [](const QString &error) {
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
            if (args.size() != (exporting ? 2 : 1))
                return fail("Expected a Markdown deck path and, for export, an output PDF path");
            QString source, error;
            if (!Backend::read(args[0], &source, &error))
                return fail(error);
            const auto deck = Deck::parse(source);
            if (exporting) {
                if (!args[1].endsWith(".pdf", Qt::CaseInsensitive))
                    return fail("The output path must end in .pdf");
                if (QFileInfo(args[1]).exists())
                    return fail("The output path already exists");
                if (!deck.exportPdf(args[1], QFileInfo(args[0]).absolutePath(),
                                    Backend::darkSlides(deck, parser.isSet("dark")), &error))
                    return fail(error);
                result.insert("path", QFileInfo(args[1]).absoluteFilePath());
            } else {
                QJsonArray slides;
                for (const auto &slide : deck.slides)
                    slides.append(QJsonObject{{"heading", slide.heading}, {"hasNotes", !slide.notes.isEmpty()}});
                result = {{"title", deck.title}, {"slideCount", deck.slides.size()}, {"slides", slides},
                          {"aspect", deck.width == 960 ? "4:3" : "16:9"}};
                if (command == "check") {
                    result.insert("warnings", QJsonArray::fromStringList(deck.warnings));
                    result.insert("ok", deck.warnings.isEmpty());
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
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    qmlRegisterType<SlideView>("Lectern", 1, 0, "SlideView");
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    if (!args.isEmpty())
        backend.open(args[0]);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
