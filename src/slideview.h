#pragma once
#include "backend.h"
#include <QQuickPaintedItem>
#include <QPointer>

class SlideView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString example MEMBER m_example NOTIFY exampleChanged)
    Q_PROPERTY(QString previewTheme MEMBER m_previewTheme NOTIFY previewThemeChanged)
    Q_PROPERTY(int slideIndex MEMBER m_slideIndex NOTIFY slideIndexChanged)
    Q_PROPERTY(Backend *deck READ deck WRITE setDeck NOTIFY deckChanged)

public:
    explicit SlideView(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {
        connect(this, &SlideView::exampleChanged, this, [this] { update(); });
        connect(this, &SlideView::previewThemeChanged, this, [this] { update(); });
        connect(this, &SlideView::slideIndexChanged, this, [this] { update(); });
    }
    Backend *deck() const { return m_deck; }
    void setDeck(Backend *deck);
    void paint(QPainter *painter) override;

signals:
    void exampleChanged();
    void previewThemeChanged();
    void deckChanged();
    void slideIndexChanged();

private:
    QString m_example, m_previewTheme;
    int m_slideIndex = -1;
    QPointer<Backend> m_deck;
    QMetaObject::Connection m_change;
    QMetaObject::Connection m_appearance;
};
