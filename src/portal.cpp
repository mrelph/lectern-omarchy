#include "portal.h"

#include <QtDBus>
#include <QUuid>

namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto requestInterface = "org.freedesktop.portal.Request";
}

void Portal::choose(const QString &action, const QString &name)
{
    if (!m_request.isEmpty())
        return;
    auto bus = QDBusConnection::sessionBus();
    const auto token = QString("lectern_") + QUuid::createUuid().toString(QUuid::Id128);
    const auto sender = bus.baseService().mid(1).replace('.', '_');
    m_request = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    m_action = action;
    if (!bus.connect(service, m_request, requestInterface, "Response",
                     this, SLOT(response(uint,QVariantMap)))) {
        m_request.clear();
        emit failed(tr("The desktop file picker is unavailable"));
        return;
    }

    const bool open = action == "open";
    const QString title = open ? tr("Open Markdown deck") : action == "pdf" ? tr("Export PDF") : tr("Save Markdown deck");
    QVariantMap options{{"handle_token", token}, {"modal", true}};
    if (!open && !name.isEmpty())
        options.insert("current_name", name);

    auto request = QDBusMessage::createMethodCall(service, "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.FileChooser",
                                                  open ? "OpenFile" : "SaveFile");
    request << QString() << title << options;
    auto watcher = new QDBusPendingCallWatcher(bus.asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, bus, token, watcher]() mutable {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (!m_request.endsWith(token))
            return;
        if (reply.isError() || reply.value().path() != m_request) {
            bus.disconnect(service, m_request, requestInterface, "Response",
                           this, SLOT(response(uint,QVariantMap)));
            m_request.clear();
            emit failed(reply.isError() ? reply.error().message() : tr("The desktop file picker returned an invalid request"));
        }
    });
}

void Portal::response(uint status, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(service, m_request, requestInterface, "Response",
                                            this, SLOT(response(uint,QVariantMap)));
    const auto action = m_action;
    m_request.clear();
    if (status == 1)
        return;
    const auto uris = qdbus_cast<QStringList>(results.value("uris"));
    const QUrl url(uris.value(0));
    if (status == 0 && url.isLocalFile())
        emit selected(action, url);
    else
        emit failed(tr("The desktop file picker could not provide a local file"));
}
