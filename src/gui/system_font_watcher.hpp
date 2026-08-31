#pragma once

#include <QObject>
#include <QQmlEngine>

namespace seabass::gui
{

// Exposes the platform's current UI font point size to QML as a live,
// NOTIFY-backed property -- Theme.qml's baseFontPointSize binds to this
// instead of Qt.application.font.pointSize directly. QQmlApplication's
// own `font` property (Qt.application.font in QML) does declare a
// fontChanged() signal, but whether anything reliably emits it when a
// *platform theme* changes the system font at runtime (as opposed to
// the app calling QGuiApplication::setFont() itself) is a long-standing
// gray area in Qt, not something worth trusting blindly for a setting
// this visible. Listening for QEvent::ApplicationFontChange directly on
// the application object sidesteps that ambiguity entirely.
class SystemFontWatcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(qreal pointSize READ pointSize NOTIFY pointSizeChanged)

public:
    explicit SystemFontWatcher(QObject *parent = nullptr);

    // Always reads QGuiApplication::font() fresh rather than returning a
    // cached value -- the font read at construction time (QML engine
    // startup) is not reliably the platform theme's fully-resolved
    // system font yet (confirmed: caching it here once produced a
    // visibly wrong, too-small size at startup), so nothing about this
    // class should assume its own past reads are trustworthy. m_lastSeen
    // exists only to suppress duplicate pointSizeChanged() emissions.
    qreal pointSize() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void pointSizeChanged();

private:
    mutable qreal m_lastSeen;
};

}  // namespace seabass::gui
