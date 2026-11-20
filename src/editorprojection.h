#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QVector>

// The full document owns Undo. This document contains only the editable view
// and translates its UTF-16 edit transactions back to source coordinates.
class EditorProjection : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void attach(QQuickTextDocument *source, QQuickTextDocument *view);
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE int toSource(int position) const;
    Q_INVOKABLE int toView(int position) const;
    Q_INVOKABLE bool allowsReplacement(int start, int end, const QString &inserted);
signals:
    void refused();

private:
    struct Range {
        int start, end;
    };
    bool allows(int start, int end);
    void refresh();
    void apply(int start, int removed, int added);
    QPointer<QTextDocument> m_source, m_view;
    QVector<Range> m_hidden;
    QString m_text;
    bool m_enabled = false;
    bool m_syncing = false;
    bool m_refreshPending = false;
};
