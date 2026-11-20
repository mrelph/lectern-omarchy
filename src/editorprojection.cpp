#include "editorprojection.h"
#include "deck.h"
#include <QScopedValueRollback>
#include <QTextCursor>

void EditorProjection::attach(QQuickTextDocument *source, QQuickTextDocument *view)
{
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    if (m_view)
        disconnect(m_view, nullptr, this, nullptr);
    m_source = source->textDocument();
    m_view = view->textDocument();
    m_view->setUndoRedoEnabled(false);
    connect(m_source, &QTextDocument::contentsChanged, this, [this] {
        if (!m_syncing)
            refresh();
    });
    connect(m_view, &QTextDocument::contentsChange, this, &EditorProjection::apply);
    // Qt has finished the view's edit transaction here. Restore a rejected
    // edit, or reproject notes newly introduced by an accepted edit.
    connect(m_view, &QTextDocument::contentsChanged, this, [this] {
        if (m_refreshPending && !m_syncing) {
            m_refreshPending = false;
            refresh();
        }
    });
    refresh();
}

void EditorProjection::setEnabled(bool enabled)
{
    m_enabled = enabled;
    refresh();
}

void EditorProjection::refresh()
{
    if (!m_source || !m_view)
        return;
    m_hidden.clear();
    if (!m_enabled)
        return;
    QScopedValueRollback guard(m_syncing, true);
    m_text = m_source->toPlainText();
    QString visible;
    int copied = 0;
    for (const auto &s : Deck::parse(m_text).slides) {
        if (s.notesStart < 0)
            continue;
        int last = s.end;
        while (last > s.notesStart && m_text[last - 1].isSpace())
            --last;
        const int newline = m_text.indexOf('\n', last);
        int end = newline < 0 ? s.end : qMin(newline + 1, s.end);
        m_hidden.append({ s.notesStart, end });
        visible += m_text.mid(copied, s.notesStart - copied);
        copied = end;
    }
    visible += m_text.mid(copied);
    if (m_view->toPlainText() != visible)
        m_view->setPlainText(visible);
}

int EditorProjection::toSource(int position) const
{
    for (const auto &r : m_hidden) {
        if (position < r.start)
            break;
        // At a trailing note without a visible next line, keep the caret
        // on the last visible character before the note.
        if (position == r.start && r.end == m_text.size() && !m_text.endsWith('\n'))
            return qMax(0, r.start - 1);
        position += r.end - r.start;
    }
    return position;
}

int EditorProjection::toView(int position) const
{
    int removed = 0;
    for (const auto &r : m_hidden) {
        if (position <= r.start)
            break;
        removed += qMin(position, r.end) - r.start;
    }
    return position - removed;
}

bool EditorProjection::allows(int start, int end)
{
    for (const auto &r : m_hidden) {
        if ((start < r.end && end >= r.start)
            || (start == r.end && r.end == m_text.size() && !m_text.endsWith('\n'))) {
            emit refused();
            return false;
        }
    }
    return true;
}

void EditorProjection::apply(int start, int removed, int added)
{
    if (m_syncing || !m_enabled || !m_source || !m_view)
        return;
    int from = toSource(start);
    int to = toSource(start + removed);
    // The view cursor can collapse onto EOF. Deleting the preceding newline
    // there would join a hidden Note: marker to the previous visible line.
    if (removed && toView(to) - toView(from) != removed) {
        emit refused();
        m_refreshPending = true;
        return;
    }
    QString inserted = m_view->toPlainText().mid(start, added);
    if (!allowsReplacement(from, to, inserted)) {
        m_refreshPending = true;
        return;
    }
    QScopedValueRollback guard(m_syncing, true);
    QTextCursor cursor(m_source);
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.insertText(inserted);
    cursor.endEditBlock();
    m_refreshPending = true;
}

bool EditorProjection::allowsReplacement(int from, int to, const QString &inserted)
{
    if (m_hidden.isEmpty())
        return true;
    if (!allows(from, to))
        return false;
    QString candidate = m_text;
    candidate.replace(from, to - from, inserted);
    // Removing a slide separator or opening a code fence in visible text
    // can also absorb a note block without touching any of its characters.
    const auto deck = Deck::parse(candidate);
    for (const auto &r : m_hidden) {
        int delta = from < r.start ? inserted.size() - (to - from) : 0;
        auto slide = std::find_if(deck.slides.cbegin(), deck.slides.cend(),
            [&](const Slide &s) { return s.notesStart == r.start + delta; });
        if (slide == deck.slides.cend()
            || !candidate.mid(slide->notesStart, slide->end - slide->notesStart)
                .startsWith(m_text.mid(r.start, r.end - r.start))
            || !candidate.mid(r.end + delta, slide->end - r.end - delta).trimmed().isEmpty()) {
            emit refused();
            return false;
        }
    }
    return true;
}
