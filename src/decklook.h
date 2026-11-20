#pragma once

#include <QPainter>
#include <QTextDocument>

struct Slide;

// Presets with their own page geometry. All dimensions are in slide coordinates.
struct DeckLook {
    enum Style { Editorial, Terminal, Dusk, Swiss, Sunrise };
    Style style;
    QString name, label, summary, font, codeFont;
    QColor paper, ink, accent, rule, subtitle, codePaper, codeInk;
    int titleSize, subtitleSize;

    static const QList<DeckLook> &all();
    static const DeckLook *find(const QString &name);
    void page(QPainter &p, QSize canvas, const QString &layout) const;
    bool title(QPainter &p, QSize canvas, QRectF area, const Slide &slide, QFont font, int fontSize) const;
    void styleDocument(QTextDocument &doc, const Slide &slide, int fontSize) const;
    void decorate(QPainter &p, QRectF text, const Slide &slide, const QTextDocument &doc, qreal scale) const;
};
