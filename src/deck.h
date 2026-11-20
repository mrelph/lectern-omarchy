#pragma once

#include <QPainter>
#include <QStringList>
#include <QVector>
#include <optional>
#include <QVariantList>

struct Slide {
    int start = 0;
    int end = 0;
    int notesStart = -1;
    QString body;
    QString notes;
    QString heading, subtitle;
    QString layout;
    QString theme;
    int themeStart = -1;
    int themeLength = 0;

    void paint(QPainter &painter, QSize canvas, bool dark, const QString &directory,
               const QString &font, const QString &codeFont, int fontSize,
               const QString &deckTheme, QStringList *warnings = nullptr, QVariantList *videos = nullptr) const;
};

struct Deck {
    QVector<Slide> slides;
    QString title;
    QString theme = "auto";
    QString transition = "fade";
    QString font;
    QString codeFont;
    int fontSize = 36;
    int headerLines = 0;
    QStringList warnings;
    int width = 1280;

    static QStringList themes();
    QString moveSlide(const QString &text, int from, int to) const;
    QStringList validate(const QString &directory) const;
    bool exportPng(const QString &folder, const QString &directory, bool dark, QString *error) const;
    static Deck parse(const QString &text);
    static std::optional<QString> replaceNotes(const QString &source, int index, const QString &notes);
    bool exportPdf(const QString &path, const QString &directory, bool dark, QString *error, bool withNotes = false, int *pages = nullptr) const;
};
