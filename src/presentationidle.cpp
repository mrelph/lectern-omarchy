#include "presentationidle.h"
#include "qwayland-idle-inhibit-unstable-v1.h"

#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QWaylandClientExtensionTemplate>
#include <qpa/qplatformwindow_p.h>
#include <wayland-client-protocol.h>

namespace {
using Manager = QtWayland::zwp_idle_inhibit_manager_v1;
using NativeWindow = QNativeInterface::Private::QWaylandWindow;

class PresentationIdle final : public QWaylandClientExtensionTemplate<PresentationIdle>, public Manager {
public:
    explicit PresentationIdle(QQuickItem *stage) : QWaylandClientExtensionTemplate(1)
    {
        connect(this, &QWaylandClientExtension::activeChanged, this, [this] {
            if (isActive()) attachSurface();
            else {
                release();
                if (isInitialized()) destroy();
            }
        });
        connect(stage, &QQuickItem::windowChanged, this, &PresentationIdle::watch);
        connect(stage, &QObject::destroyed, this, [this] { watch(nullptr); });
        watch(stage->window());
        initialize();
        if (!isActive())
            qWarning("The Wayland compositor does not advertise idle inhibition");
    }

    ~PresentationIdle() override
    {
        watch(nullptr);
        if (isInitialized()) destroy();
    }

private:
    void release()
    {
        if (m_frame) wl_callback_destroy(m_frame);
        m_frame = nullptr;
        if (m_inhibitor) zwp_idle_inhibitor_v1_destroy(m_inhibitor);
        m_inhibitor = nullptr;
    }

    void watch(QQuickWindow *window)
    {
        if (m_window) m_window->removeEventFilter(this);
        detachSurface();
        m_window = window;
        if (window) {
            window->installEventFilter(this);
            // QML reparents the stage and maps/unmaps its two windows in one
            // turn. Bind after those requests have gone to the compositor.
            QTimer::singleShot(0, this, &PresentationIdle::attachSurface);
        }
    }

    void detachSurface()
    {
        release();
        if (m_native) disconnect(m_native, nullptr, this, nullptr);
        m_native = nullptr;
    }

    void attachSurface()
    {
        if (!isActive() || !m_window || !m_window->handle()) return;
        auto *native = m_window->nativeInterface<NativeWindow>();
        if (!native) return;
        if (native != m_native) {
            detachSurface();
            m_native = native;
            connect(native, &NativeWindow::surfaceDestroyed, this, &PresentationIdle::release);
            connect(native, &NativeWindow::surfaceCreated, this, &PresentationIdle::attachSurface);
        }
        if (m_frame || m_inhibitor || !native->surface()) return;

        // A stage transfer can expose the old presenter surface while the
        // compositor is still retiring a fullscreen audience window. Wait for
        // the compositor to schedule a visible frame before binding to it.
        static const wl_callback_listener listener{[](void *data, wl_callback *, uint32_t) {
            auto *self = static_cast<PresentationIdle *>(data);
            wl_callback_destroy(self->m_frame);
            self->m_frame = nullptr;
            if (self->isActive() && self->m_native && self->m_native->surface())
                self->m_inhibitor = self->create_inhibitor(self->m_native->surface());
        }};
        m_frame = wl_surface_frame(native->surface());
        wl_callback_add_listener(m_frame, &listener, this);
        m_window->update();
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_window && event->type() == QEvent::PlatformSurface) {
            const auto *change = static_cast<QPlatformSurfaceEvent *>(event);
            if (change->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                detachSurface();
            else
                attachSurface();
        }
        return false;
    }

    QPointer<QQuickWindow> m_window;
    QPointer<NativeWindow> m_native;
    wl_callback *m_frame = nullptr;
    ::zwp_idle_inhibitor_v1 *m_inhibitor = nullptr;
};
}

std::unique_ptr<QObject> inhibitPresentationIdle(QQuickItem *stage)
{
    if (!stage || !QGuiApplication::platformName().startsWith("wayland"))
        return {};
    return std::make_unique<PresentationIdle>(stage);
}
