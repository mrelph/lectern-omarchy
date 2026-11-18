#include "slideview.h"

#include <QGuiApplication>
#include <QStyleHints>

void SlideView::setDeck(Backend *deck)
{
    if (deck == m_deck)
        return;
    disconnect(m_change);
    disconnect(m_appearance);
    m_deck = deck;
    if (deck) {
        m_change = connect(deck, &Backend::changed, this, [this] { update(); });
        m_appearance = connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged,
                               this, [this] { update(); });
    }
    emit deckChanged();
    update();
}

void SlideView::paint(QPainter *painter)
{
    if (!m_deck)
        return;
    const qreal scale = qMin(width() / m_deck->slideWidth(), height() / 720.);
    painter->translate((width() - m_deck->slideWidth() * scale) / 2, (height() - 720 * scale) / 2);
    painter->scale(scale, scale);
    m_deck->paint(*painter, qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}
