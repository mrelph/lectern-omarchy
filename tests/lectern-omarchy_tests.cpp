#include "backend.h"
#include "slideview.h"
#include "theme.h"
#include "translator.h"
#include "updater.h"

#include <cmath>
#include <QStyleHints>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFile>
#include <QRegularExpression>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class LecternTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_state;
    static bool replace(const QString &path, const QByteArray &bytes)
    {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    }

private slots:
    void initTestCase()
    {
        qApp->setOrganizationName("LecternTests");
        QVERIFY(m_state.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_state.path());
    }
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

    void exportsNotesAsPortraitHandout()
    {
        QTemporaryDir temp;
        const QString source = "# First\n\nNote:\n" + QString("A line of speaker notes.\n").repeated(120)
                               + "\n---\n\n## Second";
        const auto deck = Deck::parse(source);
        const QString path = temp.filePath("handout.pdf");
        QString error;
        QVERIFY2(deck.exportPdf(path, temp.path(), true, &error, true), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto pdf = file.readAll();
        const QRegularExpression letterBox(R"(/MediaBox \[0 0 612(?:\.0+)? 792(?:\.0+)?\])");
        QVERIFY(letterBox.match(QString::fromLatin1(pdf)).hasMatch());
        QVERIFY(pdf.count("/Type /Page\n") > 2);
    }

    void replacesOnlySelectedNotes()
    {
        const QString prefix = "## First  \r\n\r\n```text\r\nNote: code\r\n```\r\n\r\n";
        const QString suffix = "---\r\n## Second\r\n\r\nNote: keep\r\n";
        const QString source = prefix + "Notes: old\r\n\r\n" + suffix;
        const auto updated = Deck::replaceNotes(source, 0, "New notes\nSecond line");
        QVERIFY(updated);
        QCOMPARE(*updated, prefix + "Note:\r\nNew notes\r\nSecond line\r\n\r\n" + suffix);
        const auto cleared = Deck::replaceNotes(*updated, 0, "");
        QVERIFY(cleared);
        QCOMPARE(*cleared, prefix + suffix);
        QVERIFY(!Deck::replaceNotes(source, 0, "Text\n---\nMore"));
        QVERIFY(!Deck::replaceNotes(source, 0, "```text\nswallowed separator"));
        QVERIFY(!Deck::replaceNotes(source, 2, "out of range"));
        const auto appended = Deck::replaceNotes("# Last slide", 0, "Notes for last slide");
        QVERIFY(appended);
        QCOMPARE(*appended, "# Last slide\n\nNote:\nNotes for last slide");
        const auto afterBreak = Deck::replaceNotes("# First\n\n---", 1, "Empty slide notes");
        QVERIFY(afterBreak);
        QCOMPARE(*afterBreak, "# First\n\n---\nNote:\nEmpty slide notes");
    }

    void restoresRecentDeckAndListsSavedFolders()
    {
        QTemporaryDir temp;
        const auto first = temp.filePath("first.md");
        const auto second = temp.filePath("second.md");
        QVERIFY(replace(first, "# First"));
        QVERIFY(replace(second, "# Second"));
        Backend library;
        QVERIFY(library.open(second));
        QVERIFY(library.open(first));
        Backend restored;
        restored.restoreLastDeck();
        QCOMPARE(restored.title(), "First");
        auto entries = restored.library();
        QVERIFY(!entries.isEmpty());
        QCOMPARE(entries.first().toMap().value("path").toString(), first);
        QSettings settings("Lectern", "lectern-omarchy");
        settings.setValue("folders", QStringList{temp.path()});
        entries = restored.library();
        QCOMPARE(entries.size(), 3);
        QVERIFY(entries[0].toMap()["folder"].toBool());
        QCOMPARE(entries[2].toMap()["name"].toString(), "Second");
        QVERIFY(QFile::remove(second));
        QCOMPARE(restored.library().size(), 2);
        restored.forgetFolder(temp.path());
        QCOMPARE(restored.library().size(), 1);
    }

    void rendersLayoutPresets_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("aspect");
        for (const QString preset : {"editorial", "terminal", "dusk", "swiss", "sunrise"}) {
            for (const QString aspect : {"4:3", "16:9"})
                QTest::newRow(qPrintable(preset + " " + aspect)) << preset << aspect;
        }
    }

    void rendersLayoutPresets()
    {
        QFETCH(QString, preset);
        QFETCH(QString, aspect);
        QTemporaryDir temp;
        const auto source = "---\ntheme: " + preset + "\naspect: " + aspect
            + "\n---\n# A short title\n## Subtitle\nNote:\nFor the presenter\n"
              "---\n<!-- theme: dark -->\n# A different theme\n";
        Backend backend;
        const auto path = temp.filePath("deck.md");
        QVERIFY(replace(path, source.toUtf8()));
        QVERIFY(backend.open(path));
        const auto deck = Deck::parse(source);
        QVERIFY(deck.validate(temp.path()).isEmpty());
        QImage preview(deck.width, 720, QImage::Format_RGB32);
        QPainter painter(&preview);
        backend.paint(painter, false);
        painter.end();
        const QString output = temp.filePath("images");
        QString error;
        QVERIFY2(deck.exportPng(output,temp.path(),false,&error), qPrintable(error));
        QImage exported(output + "/slide-01.png");
        QVERIFY(!exported.isNull());
        const QMap<QString,QColor> papers{{"editorial","#f8f3e8"},{"terminal","#0b0e0b"},
            {"swiss","#ffffff"},{"sunrise","#fff1e6"},{"dusk","#2b1458"}};
        QCOMPARE(preview.pixelColor(preview.width()/2,0),papers.value(preset));
        QCOMPARE(exported.pixelColor(exported.width()/2,0),papers.value(preset));
        QVERIFY(QImage(output + "/slide-02.png").pixelColor(0,0) != papers.value(preset));
        if (preset == "dusk") QCOMPARE(preview.pixelColor(0,719), QColor("#10306e"));
        if (preset == "sunrise") QCOMPARE(preview.pixelColor(deck.width-1,20), QColor("#ffc9a8"));
        if (preset == "swiss") QCOMPARE(preview.pixelColor(qRound(deck.width*.075)+2,74), QColor("#d00000"));
    }

    void exportsThemedSlideImagesAndChecksMedia()
    {
        QTemporaryDir temp;
        QImage asset(80,40,QImage::Format_RGB32);
        asset.fill(QColor("#319056"));
        QVERIFY(asset.save(temp.filePath("photo.png")));
        const QString source = "---\ntheme: bold-color\naspect: 4:3\ntransition: slide\n---\n# First\n"
                               "---\n<!-- theme: blocks-dark -->\n# Second\nNote:\nHidden\n"
                               "---\n![Photo](photo.png \"50% right\")\n";
        const Deck deck = Deck::parse(source);
        QCOMPARE(deck.transition, "slide");
        QCOMPARE(deck.slides[1].theme, "blocks-dark");
        QVERIFY(deck.validate(temp.path()).isEmpty());
        const auto folder = temp.filePath("out");
        QString error;
        QVERIFY2(deck.exportPng(folder, temp.path(), false, &error), qPrintable(error));
        QImage first(folder+"/slide-01.png"), second(folder+"/slide-02.png"), third(folder+"/slide-03.png");
        QCOMPARE(first.size(), QSize(1920,1440));
        QCOMPARE(first.pixelColor(2,2), QColor("#c7381a"));
        QCOMPARE(second.pixelColor(2,2), QColor(Qt::black));
        QCOMPARE(second.pixelColor(1640,1400), QColor("#d27de3"));
        QCOMPARE(third.pixelColor(1750,720), asset.pixelColor(0,0));
        QCOMPARE(third.pixelColor(60,720), QColor("#1b2a8a"));
        QVERIFY(!deck.exportPng(folder, temp.path(), false, &error));
        QVERIFY(QFile::remove(temp.filePath("photo.png")));
        QVERIFY(deck.validate(temp.path()).join(' ').contains("Missing media"));
        const auto fenced = Deck::parse("```\n<!-- theme: blocks-dark -->\n![photo](missing.png)\n```");
        QVERIFY(fenced.slides.first().theme.isEmpty());
        QVERIFY(fenced.validate(temp.path()).isEmpty());
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
        auto *palette = window->findChild<QObject *>("commandPalette");
        QVERIFY(palette);
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(palette->property("visible").toBool());
        QTest::qWait(200);
        auto *search = window->findChild<QQuickItem *>("commandSearch");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        for (auto c : QByteArray("choose a slide theme")) QTest::keyClick(window,c);
        QCOMPARE(search->property("text").toString(), "choose a slide theme");
        QCOMPARE(window->findChild<QQuickItem *>("commandResults")->property("count").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!palette->property("visible").toBool());
        auto *gallery = window->findChild<QObject *>("themeGallery");
        QVERIFY(gallery);
        QTRY_VERIFY(gallery->property("visible").toBool());
        QTest::qWait(180);
        if (!snapshot.isEmpty())
            QVERIFY(window->grabWindow().save(snapshot + ".themes.png"));
        QVERIFY(QMetaObject::invokeMethod(gallery, "close"));
        QTRY_VERIFY(!gallery->property("visible").toBool());
        auto presenter = window->findChild<QQuickItem *>("presenterView");
        auto presenterNotes = window->findChild<QQuickItem *>("presenterNotes");
        auto audience = window->findChild<QQuickWindow *>("audienceWindow");
        auto stage = window->findChild<QQuickItem *>("presentationStage");
        auto next = window->findChild<QQuickItem *>("nextPresenterSlide");
        QVERIFY(presenter && presenterNotes && audience && stage && next);

        const auto movie = temp.filePath("preview.mp4");
        QCOMPARE(QProcess::execute("ffmpeg", {"-loglevel","error","-f","lavfi","-i","color=c=red:s=96x64:r=10",
                                              "-t","3","-c:v","mpeg4",movie}), 0);
        const auto mediaDeck = temp.filePath("media.md");
        QVERIFY(replace(mediaDeck, "![Demo](preview.mp4)"));
        QVERIFY(backend.open(mediaDeck));
        QCOMPARE(backend.videos().size(), 1);
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
        auto *layer = window->findChild<QQuickItem *>("videoLayer");
        QVERIFY(layer);
        QTRY_VERIFY(layer->isVisible());
        QQuickItem *video = nullptr;
        for (auto *child : layer->childItems())
            if (child->objectName() == "audienceVideo") video = child;
        QVERIFY(video);
        QTRY_VERIFY(video->property("position").toInt() > 0);
        auto *inputWindow = audience->isVisible() ? audience : window;
        QTest::keyClick(inputWindow, Qt::Key_B);
        QTRY_VERIFY(!layer->isVisible());
        QVERIFY(video->property("position").toInt() > 0);
        QTest::keyClick(inputWindow, Qt::Key_B);
        QTRY_VERIFY(layer->isVisible());
        window->close();
        QTRY_VERIFY(!window->property("presenting").toBool());
        QVERIFY(window->isVisible());
        backend.sample();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        if (qGuiApp->screens().size() > 1) {
            QScreen *editorScreen = window->screen();
            QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
            QTRY_VERIFY(audience->isVisible());
            QTRY_VERIFY(presenter->isVisible());
            QCOMPARE(audience->visibility(), QWindow::FullScreen);
            const auto targetName = window->property("audienceScreenName").toString();
            QVERIFY(!targetName.isEmpty() && targetName != editorScreen->name());
            QTRY_COMPARE(audience->screen()->name(), targetName);
            QCOMPARE(window->screen(), editorScreen);
            QCOMPARE(stage->window(), audience);
            QTest::keyClick(audience, Qt::Key_Right);
            QCOMPARE(backend.index(), 1);
            QCOMPARE(next->property("index").toInt(), 2);
            QCOMPARE(presenterNotes->property("text").toString(), backend.notes());
            QTest::keyClick(audience, Qt::Key_Escape);
            QTRY_VERIFY(!audience->isVisible());
            QVERIFY(!presenter->isVisible());
            window->requestActivate();
            QTRY_VERIFY(window->isActive());
            QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
            QTRY_VERIFY(audience->isVisible());
            QTRY_COMPARE(audience->screen()->name(), targetName);
            window->requestActivate();
            QTRY_VERIFY(window->isActive());
            QTest::keyClick(window, Qt::Key_Left);
            QCOMPARE(backend.index(), 0);
            audience->close();
            QTRY_VERIFY(!window->property("presenting").toBool());
            qGuiApp->removeTranslator(&translator);
            return;
        }

        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(presenter->isVisible());
        QVERIFY(!audience->isVisible());
        QVERIFY(!stage->isVisible());
        QCOMPARE(next->property("index").toInt(), 1);
        QCOMPARE(presenterNotes->property("text").toString(), backend.notes());
        QTRY_VERIFY(presenter->property("elapsed").toInt() >= 1);
        QVERIFY(QMetaObject::invokeMethod(presenter, "resetTimer"));
        QCOMPARE(presenter->property("elapsed").toInt(), 0);
        if (!snapshot.isEmpty())
            QVERIFY(window->grabWindow().save(snapshot + ".presenter.png"));
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(backend.index(), 1);
        QCOMPARE(next->property("index").toInt(), 2);
        QCOMPARE(presenterNotes->property("text").toString(), backend.notes());
        QTest::keyClick(window, Qt::Key_End);
        QCOMPARE(next->property("index").toInt(), -1);
        QTest::keyClick(window, Qt::Key_Home);
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(stage->isVisible());
        QVERIFY(!presenter->isVisible());
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(presenter->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!presenter->isVisible());
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(window->property("presenting").toBool());
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(backend.index(), 1);
        QTest::keyClick(window, Qt::Key_4);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(backend.index(), 3);
        QTest::keyClick(window, Qt::Key_9);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(backend.index(), 3);
        QTest::keyClick(window, Qt::Key_B);
        QCOMPARE(stage->property("blankColor").toString(), "black");
        QTest::keyClick(window, Qt::Key_W);
        QCOMPARE(stage->property("blankColor").toString(), "white");
        QTest::keyClick(window, Qt::Key_W);
        QVERIFY(stage->property("blankColor").toString().isEmpty());
        QTest::keyClick(window, Qt::Key_Question);
        QVERIFY(stage->property("showKeys").toBool());
        const int slideBeforeHelpNavigation = backend.index();
        QTest::keyClick(window, backend.index() == 0 ? Qt::Key_End : Qt::Key_Home);
        QCOMPARE(backend.index(), slideBeforeHelpNavigation);
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!stage->property("showKeys").toBool());
        QVERIFY(window->property("presenting").toBool());
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
        const auto original = backend.source();
        auto fontPicker = window->findChild<QQuickItem *>("slideFontPicker");
        QVERIFY(fontPicker);
        QVERIFY(fontPicker->property("count").toInt() > 1);
        const auto document = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(document);
        backend.setAppearance("font-size", "48", document);
        QCOMPARE(backend.slideSize(), 48);
        QCOMPARE(backend.count(), 6);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(backend.source(), original);
        backend.setAppearance("theme", "dark", document);
        QCOMPARE(backend.slideTheme(), "dark");
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(backend.source(), original);
        const auto oldNotes = backend.notes();
        auto notes = window->findChild<QQuickItem *>("notesEditor");
        QVERIFY(notes);
        notes->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(!window->property("presenting").toBool());
        QVERIFY(notes->setProperty("text", "Updated speaker notes"));
        QVERIFY(backend.dirty());
        editor->forceActiveFocus();
        QCOMPARE(backend.notes(), "Updated speaker notes");
        QVERIFY(backend.source().contains("Note:\nUpdated speaker notes"));
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(backend.source(), original);
        QCOMPARE(backend.notes(), oldNotes);
        QCOMPARE(notes->property("text").toString(), oldNotes);
        QVERIFY(!backend.dirty());
        QVERIFY(replace(language, "da\n"));
        QTRY_COMPARE(translator.language(), "da");
        QCOMPARE(translator.translate("Main", "Preview"), "Forhåndsvisning");
        if (!snapshot.isEmpty())
            QVERIFY(window->grabWindow().save(snapshot + ".da.png"));
        qGuiApp->removeTranslator(&translator);
    }
    void protectsCollapsedSpeakerNotes()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath("notes.md");
        const QString original = "# One 🐋\n\n~~~\nNote: literal\n~~~\n\nNote:\nprivate\n\n---\n# Two";
        QVERIFY(replace(path, original.toUtf8()));
        Theme theme(temp.path());
        Translator translator(temp.filePath("language"));
        Backend backend;
        Updater updater("test", {});
        QVERIFY(backend.open(path));
        qmlRegisterType<SlideView>("Lectern", 1, 0, "SlideView");
        for (int launch = 0; launch < 2; ++launch) {
            QQmlApplicationEngine engine;
            engine.rootContext()->setContextProperty("backend", &backend);
            engine.rootContext()->setContextProperty("translator", &translator);
            engine.rootContext()->setContextProperty("theme", &theme);
            engine.rootContext()->setContextProperty("updater", &updater);
            engine.load(QUrl("qrc:/Main.qml"));
            QVERIFY(!engine.rootObjects().isEmpty());
            auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(QTest::qWaitForWindowExposed(window));
            auto compact = window->findChild<QQuickItem *>("collapsedNotesEditor");
            auto editor = window->findChild<QQuickItem *>("editor");
            auto notes = window->findChild<QQuickItem *>("notesEditor");
            QVERIFY(compact && editor && notes);
            if (launch == 0) {
                QVERIFY(!compact->isVisible());
                QTest::keyClick(window, Qt::Key_N, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
            }
            QTRY_VERIFY(compact->isVisible());
            QCOMPARE(compact->property("text").toString(), QString(original).remove("Note:\nprivate\n"));
            QCOMPARE(backend.source(), original);
            const QString imagePath = temp.filePath("picture.png");
            QImage image(4, 4, QImage::Format_RGB32);
            image.fill(Qt::red);
            QVERIFY(image.save(imagePath));
            auto paste = [&](QQuickItem *area) {
                auto *mime = new QMimeData;
                mime->setUrls({QUrl::fromLocalFile(imagePath)});
                qGuiApp->clipboard()->setMimeData(mime);
                area->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
            };
            auto drop = [&](QQuickItem *area) {
                QMimeData mime;
                mime.setUrls({QUrl::fromLocalFile(imagePath)});
                const auto point = area->mapToScene(QPointF(35, 24));
                QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, {});
                QCoreApplication::sendEvent(window, &enter);
                QDragMoveEvent move(point, Qt::CopyAction, &mime, Qt::LeftButton, {});
                QCoreApplication::sendEvent(window, &move);
                QDropEvent event(point, Qt::CopyAction, &mime, Qt::LeftButton, {});
                QCoreApplication::sendEvent(window, &event);
            };
            const QString asset = temp.filePath("assets/picture.png");
            auto exerciseMedia = [&](QQuickItem *area) {
                for (bool useDrop : {false, true}) {
                    const bool preexisting = QFileInfo::exists(asset);
                    area->setProperty("cursorPosition", 0);
                    if (useDrop) drop(area);
                    else paste(area);
                    QVERIFY(backend.source().contains("](assets/picture.png)"));
                    QVERIFY(QFileInfo::exists(asset));
                    QVERIFY(backend.source().contains("Note:\nprivate"));
                    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
                    QCOMPARE(backend.source(), original);
                    QCOMPARE(QFileInfo::exists(asset), preexisting);
                    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
                    QVERIFY(backend.source().contains("](assets/picture.png)"));
                    QVERIFY(QFileInfo::exists(asset));
                    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
                    QFile saved(path);
                    QVERIFY(saved.open(QIODevice::ReadOnly));
                    QCOMPARE(saved.readAll(), backend.source().toUtf8());
                    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
                    QCOMPARE(backend.source(), original);
                    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
                    QFile restored(path);
                    QVERIFY(restored.open(QIODevice::ReadOnly));
                    QCOMPARE(restored.readAll(), original.toUtf8());
                }
            };
            if (launch == 1) {
                QVERIFY(QMetaObject::invokeMethod(window, "toggleEditorNotes"));
                QVERIFY(!compact->isVisible());
                QCOMPARE(editor->property("text").toString(), original);
                exerciseMedia(window->findChild<QQuickItem *>("fullSourceEditor"));
                break;
            }
            compact->forceActiveFocus();
            int start = original.indexOf("Note:\nprivate");
            int end = original.indexOf("private") + 8;
            compact->setProperty("cursorPosition", start - 1);
            QTest::keyClick(window, Qt::Key_Right);
            QCOMPARE(editor->property("cursorPosition").toInt(), end);
            QTest::keyClick(window, Qt::Key_Left);
            QCOMPARE(editor->property("cursorPosition").toInt(), start - 1);
            QRectF caret;
            QVERIFY(QMetaObject::invokeMethod(compact, "positionToRectangle", Q_RETURN_ARG(QRectF, caret),
                                              Q_ARG(int, start)));
            QTest::mouseClick(window, Qt::LeftButton, {}, compact->mapToScene(caret.center()).toPoint());
            QCOMPARE(editor->property("cursorPosition").toInt(), end);
            QSignalSpy refused(backend.editorProjection(), &EditorProjection::refused);
            for (auto key : {Qt::Key_Delete, Qt::Key_X, Qt::Key_V}) {
                QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
                qGuiApp->clipboard()->setText("pasted");
                QTest::keyClick(window, key, key == Qt::Key_V ? Qt::ControlModifier : Qt::NoModifier);
                QCOMPARE(backend.source(), original);
                QCOMPARE(compact->property("text").toString(), QString(original).remove("Note:\nprivate\n"));
            }
            QCOMPARE(refused.count(), 3);
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            paste(compact);
            drop(compact);
            QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier);
            QVERIFY(refused.count() >= 6);
            QCOMPARE(backend.source(), original);
            QVERIFY(!QFileInfo::exists(asset));
            exerciseMedia(compact);
            compact->setProperty("cursorPosition", 0);
            QTest::keyClick(window, Qt::Key_X);
            QCOMPARE(backend.source(), "x" + original);
            QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
            QCOMPARE(backend.source(), original);
            notes->forceActiveFocus();
            notes->setProperty("text", "changed in notes panel");
            compact->forceActiveFocus();
            QCOMPARE(backend.notes(), "changed in notes panel");
            QVERIFY(!compact->property("text").toString().contains("changed in notes panel"));
            QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
            QCOMPARE(backend.source(), original);
            QVERIFY(!backend.dirty());
        }
    }

    void applicationAppearance()
    {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("theme"));
        const auto toml = temp.filePath("theme/colors.toml");
        QVERIFY(replace(toml, "mode = 'dark'\nbackground = '#123456'\naccent = '#102034'\n"));
        Theme theme(temp.path());
        QCOMPARE(theme.appearance(), "system");
        QCOMPARE(theme.interfacePalette(), "omarchy");
        QCOMPARE(theme.colors()["reader"].value<QColor>(), QColor("#123456"));
        QCOMPARE(QColor(theme.accent()), QColor("#102034"));

        Backend backend;
        const auto path = temp.filePath("example.md");
        const QByteArray bytes = "---\r\ntheme: auto\r\n---\r\n# Sky\r\n\r\nNote: clouds\r\n";
        QVERIFY(replace(path, bytes));
        QVERIFY(backend.open(path));
        const QString source = backend.source();
        const auto systemScheme = qGuiApp->styleHints()->colorScheme();
        auto png = [&](const QString &folder) {
            QString error;
            if (!Deck::parse(backend.source()).exportPng(temp.filePath(folder), temp.path(),
                    qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark, &error))
                return QImage();
            return QImage(temp.filePath(folder + "/slide-01.png"));
        };
        const auto originalPng = png("before");
        QVERIFY(!originalPng.isNull());
        SlideView preview;
        preview.setDeck(&backend);
        preview.setSize({320, 180});
        auto render = [&] {
            QImage image(320, 180, QImage::Format_RGB32);
            image.fill(Qt::black);
            QPainter painter(&image);
            preview.paint(&painter);
            return image;
        };
        const auto originalPreview = render();

        Translator translator(temp.filePath("language"));
        Updater updater("test", {});
        qmlRegisterType<SlideView>("Lectern", 1, 0, "SlideView");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("updater", &updater);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *dialog = window->findChild<QObject *>("appAppearanceDialog");
        auto *modePicker = window->findChild<QObject *>("appAppearanceMode");
        auto *palettePicker = window->findChild<QObject *>("appInterfacePalette");
        auto *notes = window->findChild<QObject *>("notesEditor");
        auto *library = window->findChild<QObject *>("libraryQuery");
        auto *strip = window->findChild<QObject *>("slideStrip");
        auto *presenter = window->findChild<QObject *>("presenterView");
        auto *editor = window->findChild<QObject *>("fullSourceEditor");
        QVERIFY(dialog && modePicker && palettePicker && notes && presenter && editor && library && strip);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QVERIFY(QMetaObject::invokeMethod(modePicker, "activated", Q_ARG(int, 1)));
        QVERIFY(QMetaObject::invokeMethod(palettePicker, "activated", Q_ARG(int, 2)));
        QCOMPARE(theme.appearance(), "light");
        QCOMPARE(theme.interfacePalette(), "paper");
        QCOMPARE(window->color(), QColor("#f3ead8"));
        QCOMPARE(dialog->property("background").value<QQuickItem *>()->property("color").value<QColor>(), QColor("#ece0c8"));
        QCOMPARE(presenter->property("panel").value<QColor>(), QColor("#ece0c8"));
        QCOMPARE(editor->property("color").value<QColor>(), theme.colors()["text"].value<QColor>());
        QCOMPARE(library->property("selectionColor").value<QColor>(), theme.colors()["selection"].value<QColor>());
        auto *selected = strip->property("currentItem").value<QQuickItem *>();
        QVERIFY(selected);
        auto *highlight = selected->property("background").value<QQuickItem *>();
        QVERIFY(highlight);
        QCOMPARE(highlight->property("color").value<QColor>(), theme.colors()["selection"].value<QColor>());
        QCOMPARE(notes->property("selectionColor").value<QColor>(), theme.colors()["selection"].value<QColor>());
        QCOMPARE(editor->property("selectedTextColor").value<QColor>(), theme.colors()["onSelection"].value<QColor>());
        Theme restarted(temp.path());
        QCOMPARE(restarted.appearance(), "light");
        QCOMPARE(restarted.interfacePalette(), "paper");

        auto luminance = [](QColor c) {
            auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v+.055)/1.055, 2.4); };
            return .2126*linear(c.redF()) + .7152*linear(c.greenF()) + .0722*linear(c.blueF());
        };
        auto contrast = [&](QColor a, QColor b) {
            const auto x = luminance(a), y = luminance(b);
            return (std::max(x,y)+.05)/(std::min(x,y)+.05);
        };
        for (const auto name : {"omarchy","inkwell","paper","slate","forest","midnight"}) {
            theme.setInterfacePalette(name);
            for (const auto mode : {"dark","light"}) {
                theme.setAppearance(mode);
                const auto colors = theme.colors();
                auto c = [&](QString key) { return colors.value(key).value<QColor>(); };
                for (const auto surface : {"reader","panel","chrome","canvas"}) {
                    for (const auto role : {"text","muted","focus"})
                        QVERIFY2(contrast(c(role), c(surface)) >= 4.5, qPrintable(QString("%1/%2: %3 on %4").arg(name, mode, role, surface)));
                    QVERIFY(contrast(c("selection"), c(surface)) >= 3);
                }
                QVERIFY(contrast(c("selection"), c("onSelection")) >= 4.5);
                QVERIFY(contrast(c("accent"), c("onAccent")) >= 4.5);
            }
        }
        QCOMPARE(qGuiApp->styleHints()->colorScheme(), systemScheme);
        QCOMPARE(render(), originalPreview);
        QCOMPARE(png("after"), originalPng);
        QCOMPARE(backend.source(), source);
        QVERIFY(!backend.dirty());
        QFile deckFile(path);
        QVERIFY(deckFile.open(QIODevice::ReadOnly));
        QCOMPARE(deckFile.readAll(), bytes);

        backend.edit(source + "draft", 0);
        const auto draft = backend.source();
        QVERIFY(backend.dirty());
        theme.setAppearance("system");
        theme.setInterfacePalette("omarchy");
        QVERIFY(replace(toml, "mode = 'light'\nbackground = '#faf5ef'\naccent = '#aa3344'\n"));
        QTRY_COMPARE(QColor(theme.accent()), QColor("#aa3344"));
        QVERIFY(!theme.colors()["dark"].toBool());
        QCOMPARE(theme.colors()["reader"].value<QColor>(), QColor("#faf5ef"));
        QCOMPARE(window->color(), theme.colors()["chrome"].value<QColor>());
        QCOMPARE(backend.source(), draft);
        QVERIFY(backend.dirty());
    }

};

QTEST_MAIN(LecternTests)
#include "lectern-omarchy_tests.moc"
