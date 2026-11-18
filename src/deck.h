#pragma once

#include <QPainter>
#include <QStringList>
#include <QVector>

struct Slide {
    int start = 0;
    QString body;
    QString notes;
    QString heading;

    void paint(QPainter &painter, QSize canvas, bool dark, const QString &directory,
               const QString &font, const QString &codeFont) const;
};

struct Deck {
    QVector<Slide> slides;
    QString title;
    QString theme = "auto";
    QString font;
    QString codeFont;
    QStringList warnings;
    int width = 1280;

    static Deck parse(const QString &text);
    bool exportPdf(const QString &path, const QString &directory, bool dark, QString *error) const;
};
