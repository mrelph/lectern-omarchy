#pragma once

#include <memory>

class QObject;
class QQuickItem;

// The returned handle inhibits idle on the stage's current Wayland surface.
std::unique_ptr<QObject> inhibitPresentationIdle(QQuickItem *stage);
