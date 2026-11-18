#include "backend.h"
#include "slideview.h"
#include "theme.h"
#include "translator.h"
#include "updater.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

class LecternTests : public QObject {
    Q_OBJECT

private:
    static bool replace(const QString &path, const QByteArray &bytes)
    {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    }

private slots:
    void readsLecternFormat()
    {
        const QString source = "---\r\ntitle: \"Night sky\" # deck\r\naspect: '4:3'\r\n---\r\n"
                               "# Sky\r\n\r\n~~~text\r\n---\r\nNote: keep visible\r\n~~~\r\n"
                               "Notes: presenter only\r\n---\r\n\r\n## Stars\r\n";
        const Deck deck = Deck::parse(source);
        QCOMPARE(deck.title, "Night sky");
        QCOMPARE(deck.width, 960);
        QCOMPARE(deck.slides.size(), 2);
        QCOMPARE(deck.slides[0].notes, "presenter only");
        QVERIFY(deck.slides[0].body.contains("Note: keep visible"));
        QCOMPARE(deck.slides[1].heading, "Stars");
        QCOMPARE(deck.slides[1].start, source.indexOf("\r\n## Stars"));
        QVERIFY(deck.warnings.isEmpty());
    }

    void reloadsExternalEditsAndKeepsUnsavedWork()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath("slides.md");
        QVERIFY(replace(path, "# Original\r\n\r\n---\r\n\r\n## Next\r\n"));
        Backend backend;
        QVERIFY(backend.open(path));
        backend.followCursor(backend.source().replace("\r\n", "\n").indexOf("## Next"));
        QCOMPARE(backend.index(), 1);
        backend.edit("# Saved\n", 0);
        backend.save();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), "# Saved\r\n");
        file.close();
        QVERIFY(replace(path, "# External\n"));
        QTRY_COMPARE(backend.title(), "External");
        backend.edit("# Local\n", 0);
        QVERIFY(replace(path, "# Colliding\n"));
        QTRY_VERIFY(backend.status().contains("changed on disk"));
        backend.save();
        QCOMPARE(backend.title(), "Local");
        QVERIFY(backend.dirty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), "# Colliding\n");
    }

    void rendersOnePdfPagePerSlide()
    {
        QTemporaryDir temp;
        const auto deck = Deck::parse("# First\n\n---\n\n## Last\n");
        const QString path = temp.filePath("deck.pdf");
        QString error;
        QVERIFY2(deck.exportPdf(path, temp.path(), false, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto pdf = file.readAll();
        QVERIFY(pdf.startsWith("%PDF-"));
        QCOMPARE(pdf.count("/Type /Page\n"), 2);
    }

    void editsAndPresentsWithQml()
    {
        QTemporaryDir temp;
        const QString language = temp.filePath("language");
        QVERIFY(replace(language, "en\n"));
        Translator translator(language);
        qGuiApp->installTranslator(&translator);
        Theme theme(temp.path());
        Backend backend;
        backend.sample();
        QQuickStyle::setStyle("Material");
        qmlRegisterType<SlideView>("Lectern", 1, 0, "SlideView");
        Updater updater("0.2.0", {});
        QQmlApplicationEngine engine;
        QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("updater", &updater);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto snapshot = qEnvironmentVariable("LECTERN_TEST_SCREENSHOT");
        if (!snapshot.isEmpty())
            QVERIFY(window->grabWindow().save(snapshot));
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(window->property("presenting").toBool());
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(backend.index(), 1);
        QTest::keyClick(window, Qt::Key_End);
        QCOMPARE(backend.index(), backend.count() - 1);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("presenting").toBool());
        auto editor = window->findChild<QQuickItem *>("editor");
        QVERIFY(editor);
        editor->forceActiveFocus();
        QVERIFY(editor->setProperty("cursorPosition", 0));
        QTest::keyClick(window, Qt::Key_X);
        QVERIFY(backend.dirty());
        QVERIFY(backend.source().startsWith("x"));
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!backend.dirty());
        QVERIFY(replace(language, "da\n"));
        QTRY_COMPARE(translator.language(), "da");
        QCOMPARE(translator.translate("Main", "Preview"), "Forhåndsvisning");
        if (!snapshot.isEmpty())
            QVERIFY(window->grabWindow().save(snapshot + ".da.png"));
        qGuiApp->removeTranslator(&translator);
    }
};

QTEST_MAIN(LecternTests)
#include "lectern-omarchy_tests.moc"
