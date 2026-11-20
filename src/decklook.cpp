#include "decklook.h"
#include "deck.h"
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QRegularExpression>

const QList<DeckLook> &DeckLook::all()
{
    static const QList<DeckLook> looks{
        {Editorial, "editorial", "Editorial", "Serif headings, wide margins and a red rule on cream.", "bookerly", "system",
         "#f8f3e8", "#1d1a17", "#9e1b1b", "#9e1b1b", "#5b5148", "#1d1a17", "#f8f3e8", 104, 38},
        {Terminal, "terminal", "Terminal", "Green prompt lines and amber accents on near-black.", "geist-mono", "geist-mono",
         "#0b0e0b", "#7cf29a", "#ffb547", "#ffb547", "#7a9a83", "#0b0e0b", "#c8f5d2", 72, 32},
        {Dusk, "dusk", "Dusk", "Centered titles over a purple-to-blue gradient.", "ember", "geist-mono",
         "#2b1458", "#ffffff", "#ff8a73", "#ff8a73", "#ffb4a5", "#140b2e", "#f3efff", 96, 40},
        {Swiss, "swiss", "Swiss", "Bottom-left titles, a strict grid and one red block.", "roboto", "geist-mono",
         "#ffffff", "#000000", "#d00000", "#d00000", "#555555", "#f0f0f0", "#000000", 112, 32},
        {Sunrise, "sunrise", "Sunrise", "Plum text, soft circles and rounded panels on peach.", "ubuntu", "geist-mono",
         "#fff1e6", "#3d2433", "#b03c16", "#ff8a5c", "#77566a", "#3d2433", "#fff1e6", 92, 40}
    };
    return looks;
}

const DeckLook *DeckLook::find(const QString &name)
{
    for (const auto &look : all())
        if (look.name == name) return &look;
    return nullptr;
}

static void roundBox(QPainter &p, QRectF rect, QColor color, qreal radius)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(rect, radius, radius);
    p.restore();
}

void DeckLook::page(QPainter &p, QSize canvas, const QString &layout) const
{
    QRectF bounds(QPointF(), canvas);
    if (style == Dusk) {
        QLinearGradient gradient(0, 0, 0, canvas.height());
        gradient.setColorAt(0, paper);
        gradient.setColorAt(1, QColor("#10306e"));
        p.fillRect(bounds, gradient);
    } else {
        p.fillRect(bounds, style == Editorial && layout == "code" ? codePaper : paper);
    }
    auto circle = [&](double x, double y, double r, QColor color, double alpha) {
        color.setAlphaF(alpha);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(x*canvas.width(), y*canvas.height()), r*canvas.height(), r*canvas.height());
        p.restore();
    };
    if (layout == "title" && style == Dusk) {
        circle(.92, 1.02, .55, rule, .14);
        circle(.2, -.1, .3, ink, .05);
    } else if (style == Sunrise) {
        if (layout == "title") {
            circle(.9,.08,.42,"#ffc9a8",1);
            circle(.97,.62,.14,rule,.35);
            circle(.04,.98,.2,"#ffc9a8",.7);
        } else if (layout == "content" || layout == "code") {
            circle(.98,.02,.2,"#ffc9a8",.7);
        }
    }
}

bool DeckLook::title(QPainter &p, QSize canvas, QRectF area, const Slide &slide, QFont font, int fontSize) const
{
    QTextDocument headline, subhead;
    const bool hasSubtitle = !slide.subtitle.isEmpty();
    qreal gap = 0, height = 0, h1 = 0, h2 = 0;
    const qreal extra = style == Dusk && hasSubtitle ? 6 : style == Editorial ? 42 : style == Sunrise ? 44 : 0;
    const double reserve = style == Swiss ? canvas.height()*.22 + 36 : 0;
    bool fits = false;
    for (int size = qRound(titleSize * fontSize / 36.); size >= 12; size -= 2) {
        const qreal factor = qreal(size) / titleSize;
        auto prepare = [&](QTextDocument &doc, const QString &text, bool sub) {
            doc.clear();
            doc.setDocumentMargin(0);
            doc.setTextWidth(area.width());
            QFont face(font);
            face.setPixelSize(sub ? qMax(12, qRound(subtitleSize * factor)) : size);
            face.setBold(sub ? style == Swiss : style != Editorial);
            face.setItalic(sub && style == Editorial);
            doc.setDefaultFont(face);
            QTextCursor cursor(&doc);
            QTextBlockFormat block;
            block.setAlignment(style == Dusk ? Qt::AlignHCenter : Qt::AlignLeft);
            cursor.setBlockFormat(block);
            QTextCharFormat format;
            if (style == Terminal) {
                format.setForeground(sub ? subtitle : accent);
                cursor.insertText(sub ? "# " : "$ ", format);
            }
            format.setForeground(sub ? subtitle : ink);
            cursor.insertText(text, format);
            if (!sub && style == Terminal) {
                format.setForeground(accent);
                cursor.insertText(" █", format);
            }
        };
        prepare(headline, slide.heading, false);
        if (hasSubtitle) prepare(subhead, slide.subtitle, true);
        h1 = headline.size().height();
        h2 = hasSubtitle ? subhead.size().height() : 0;
        gap = hasSubtitle ? 28*factor : 0;
        height = h1 + h2 + gap + extra + (style == Dusk ? gap : 0);
        fits = height <= area.height() - reserve;
        if (fits) break;
    }
    auto draw = [&](QTextDocument &doc, qreal y) {
        p.save();
        p.translate(area.left(), y);
        doc.drawContents(&p);
        p.restore();
    };
    qreal y = area.center().y() - height/2;
    if (style == Swiss) {
        const qreal side = canvas.height()*.22;
        p.fillRect(QRectF(area.left(),area.top(),side,side),rule);
        y = area.bottom()-height;
        p.fillRect(QRectF(area.left(),y-28,area.width(),3),ink);
        if (hasSubtitle) draw(subhead,y);
        draw(headline,y + h2 + gap);
        return fits;
    }
    if (style == Editorial) {
        y = qBound(area.top(), y + canvas.height()*.08, qMax(area.top(),area.bottom()-height));
        p.fillRect(QRectF(area.left(),y,area.width(),2),rule);
        y += extra;
    } else if (style == Sunrise) {
        roundBox(p,{area.left(),y,96,14},rule,7);
        y += extra;
    } else if (style == Terminal) {
        p.save();
        p.setPen(QPen(QColor("#3d5a45"),2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(QPointF(),canvas).adjusted(28,28,-28,-28),14,14);
        p.restore();
    }
    draw(headline,y);
    if (hasSubtitle) {
        y += h1 + gap;
        if (style == Dusk) {
            roundBox(p,{area.center().x()-40,y,80,6},rule,3);
            y += 6 + gap;
        }
        draw(subhead,y);
    }
    return fits;
}

void DeckLook::styleDocument(QTextDocument &doc, const Slide &slide, int fontSize) const
{
    const bool quote = slide.layout == "quote";
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        auto paragraph = block.blockFormat();
        if ((block == doc.begin() && paragraph.headingLevel()) || quote) {
            if (style == Dusk) paragraph.setAlignment(Qt::AlignHCenter);
            if (quote) paragraph.setLeftMargin(0);
            if (!quote && style == Editorial) paragraph.setBottomMargin(32);
            cursor.setBlockFormat(paragraph);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat face;
            if (!quote) {
                face.setProperty(QTextFormat::FontPixelSize, (style == Editorial || style == Swiss ? 64. : 60.) * fontSize / 36.);
                face.setFontWeight(style == Editorial ? QFont::Normal : QFont::Bold);
            } else {
                face.setFontItalic(style == Editorial || style == Dusk);
                face.setFontWeight(style == Swiss ? QFont::Bold : QFont::Normal);
            }
            cursor.mergeCharFormat(face);
            if (!quote && style == Terminal) {
                cursor.setPosition(block.position());
                face.setForeground(subtitle);
                cursor.insertText("## ",face);
            }
        }
        auto *list = block.textList();
        if (!list || list->format().indent() != 1 || paragraph.marker() != QTextBlockFormat::MarkerType::NoMarker)
            continue;
        auto format = list->format();
        if (format.style() != QTextListFormat::ListDisc && format.style() != QTextListFormat::ListCircle)
            continue;
        if (style == Swiss) {
            format.setStyle(QTextListFormat::ListSquare);
            list->setFormat(format);
        } else if (style == Editorial || style == Terminal) {
            list->remove(block);
            cursor.setPosition(block.position());
            auto hanging = cursor.blockFormat();
            hanging.setIndent(0);
            hanging.setLeftMargin(fontSize);
            hanging.setTextIndent(-fontSize);
            cursor.setBlockFormat(hanging);
            QTextCharFormat marker;
            marker.setForeground(accent);
            cursor.insertText(style == Terminal ? ">  " : "–  ", marker);
        }
    }
}

void DeckLook::decorate(QPainter &p, QRectF text, const Slide &slide, const QTextDocument &doc, qreal scale) const
{
    if (slide.layout == "code") {
        const QRectF panel = text.adjusted(-32,-28,32,28);
        if (style == Dusk || style == Sunrise)
            roundBox(p,panel,codePaper,style == Dusk ? 24 : 36);
        if (style == Swiss) {
            p.fillRect(panel,codePaper);
            p.fillRect(QRectF(panel.left(),panel.top()-8,120,8),rule);
        }
        if (style == Editorial) p.fillRect(QRectF(text.left(),24,64,2),rule);
        static const QRegularExpression fence(R"(^\s*(?:`{3,}|~{3,})\s*([\w.+-]+))");
        const auto lang = fence.match(slide.body).captured(1);
        const auto label = style == Terminal ? "$ cat snippet" + (lang.isEmpty() ? QString() : "." + lang) : lang.toUpper();
        QFont font(doc.defaultFont());
        font.setPixelSize(style == Terminal ? 22 : 18);
        p.setFont(font);
        p.setPen(style == Terminal ? accent : style == Editorial ? codeInk : subtitle);
        p.drawText(QPointF(text.left(), qMin(56., panel.top()-8)),label);
    } else if (slide.layout == "quote") {
        if (style == Terminal || style == Swiss) {
            p.fillRect(QRectF(text.left()-44,text.top(),style == Swiss ? 20 : 6,text.height()),rule);
        } else {
            if (style == Sunrise)
                roundBox(p,text.adjusted(-46,-36,46,36),"#ffdbbf",44);
            QFont mark(doc.defaultFont());
            mark.setPixelSize(qRound((style == Editorial ? 130 : 104)*scale));
            p.setFont(mark);
            p.setPen(rule);
            p.drawText(QPointF(style == Dusk ? text.center().x()-24 : text.left()-22, qMax(66.,text.top()+10)),QString(QChar(0x201c)));
        }
    } else if (slide.layout == "content" && doc.begin().blockFormat().headingLevel()) {
        if (style == Editorial) {
            const qreal y = doc.documentLayout()->blockBoundingRect(doc.begin()).bottom()*scale + text.top()+8;
            p.fillRect(QRectF(text.left(),y,text.width(),1.5),rule);
        } else if (style == Swiss) {
            p.fillRect(QRectF(text.left(),text.top()-18,text.width(),3),ink);
            p.fillRect(QRectF(text.left(),text.top()-22,120,11),rule);
        } else if (style == Sunrise || style == Dusk) {
            const bool centered = style == Dusk;
            roundBox(p,{centered ? text.center().x()-28 : text.left(),text.top()-19,centered ? 56. : 64.,centered ? 6. : 12.},rule,6);
        }
    }
}
