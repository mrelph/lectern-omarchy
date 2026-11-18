#pragma once
#include "backend.h"
#include <QQuickPaintedItem>
#include <QPointer>

class SlideView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *deck READ deck WRITE setDeck NOTIFY deckChanged)

public:
    using QQuickPaintedItem::QQuickPaintedItem;
    Backend *deck() const { return m_deck; }
    void setDeck(Backend *deck);
    void paint(QPainter *painter) override;

signals:
    void deckChanged();

private:
    QPointer<Backend> m_deck;
    QMetaObject::Connection m_change;
    QMetaObject::Connection m_appearance;
};
