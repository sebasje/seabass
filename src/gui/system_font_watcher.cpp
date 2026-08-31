#include "system_font_watcher.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <qglobal.h>

namespace seabass::gui
{

SystemFontWatcher::SystemFontWatcher(QObject *parent)
    : QObject(parent), m_lastSeen(QGuiApplication::font().pointSizeF())
{
    // Installed on the whole application, not a specific window: the
    // font-change event this is watching for is a QGuiApplication-level
    // notification, not tied to any one QWindow.
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

qreal SystemFontWatcher::pointSize() const
{
    m_lastSeen = QGuiApplication::font().pointSizeF();
    return m_lastSeen;
}

bool SystemFontWatcher::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationFontChange) {
        const qreal updated = QGuiApplication::font().pointSizeF();
        if (!qFuzzyCompare(updated, m_lastSeen)) {
            m_lastSeen = updated;
            emit pointSizeChanged();
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace seabass::gui
