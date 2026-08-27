#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

namespace
{

// Qt Quick Controls' Material style resolves its palette from these
// environment variables exactly once, the first time a Material-styled
// item is instantiated -- and that includes every Popup-based control
// (Dialog, ComboBox's dropdown, Menu, ToolTip), which does NOT reliably
// inherit Material.theme/accent/etc set as QML attached properties on the
// ApplicationWindow (Popups attach to the window's overlay, not its item
// tree, so that inheritance chain doesn't reach them). Setting the style
// globally here, before QGuiApplication exists, is what actually makes
// every Material-styled surface in the app -- not just the ones directly
// under the window -- consistently follow either "Kelp" (this app's own
// always-dark palette) or the system's own light/dark choice.
//
// Deliberately only sets THEME + ACCENT/PRIMARY, not BACKGROUND/
// FOREGROUND -- overriding those too caused widespread dark-on-dark and
// light-on-light contrast bugs (disabled-state colors, hint text, row
// alternation and more all broke at once), because Material's style
// implementation derives several other colors *from* its own default
// background/foreground relationship, not just from the two colors
// directly. Standard Material Dark's own background/foreground are close
// enough to "Kelp" for Popups (the only place this env var actually
// matters -- everything else uses Theme.qml's exact palette directly),
// and staying internally consistent matters far more there than an exact
// color match.
//
// Must run before the QGuiApplication is constructed -- Qt Quick
// Controls reads these at first use, effectively at process startup.
void exportMaterialPalette()
{
    // Same file AppSettingsController's own (default-constructed)
    // QSettings resolves to -- duplicated here as a literal path rather
    // than relying on organizationName/applicationName being set up by
    // the time this runs (they aren't, this executes before
    // QGuiApplication exists at all).
    QSettings settings(QDir::homePath() + "/.config/djconvert/djconvert-gui.conf", QSettings::IniFormat);
    bool useSystemTheme = settings.value("useSystemTheme", false).toBool();

    // "Current"/"Abyss" -- this app's own brand colors (see Theme.qml),
    // applied regardless of theme, same as the QML-level Material.accent/
    // primary bindings in Main.qml.
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#3daee9");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_PRIMARY", "#123a52");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", useSystemTheme ? "System" : "Dark");

    // Deliberately NOT setting QT_QUICK_CONTROLS_STYLE: tried forcing it
    // to "Material" (plus a matching qtquickcontrols2.conf) to fix a
    // separate, narrower issue -- ProgressBar loading KDE's own
    // "org.kde.breeze" style and throwing harmless-but-noisy TypeErrors
    // (breeze's ProgressBar.qml assumes an anchoring context Material's
    // equivalent doesn't set up) -- but forcing the style did NOT fix
    // that (KDE's platform theme integration still overrode it for
    // ProgressBar specifically) and DID visibly break every other
    // control: Buttons rendered as Material's default pill-shaped
    // outlined buttons instead of this app's flat rectangular cards.
    // Net negative, reverted. The ProgressBar warning is left as a
    // known, cosmetic-only, unresolved issue -- see project memory.
}

}  // namespace

int main(int argc, char **argv)
{
    exportMaterialPalette();
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/DjConvertGui/qml/icons/seabass_soundbass.svg")));

    // Two djconvert-gui instances writing to the same stick at once is
    // exactly the corruption risk StickWriteLock exists to prevent -- that
    // lock alone already covers it correctly, but refusing a second
    // instance outright is cheaper and clearer than letting someone open
    // two windows and wonder why writes keep failing.
    QString lockDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(lockDir);
    QLockFile instanceLock(lockDir + "/djconvert-gui.lock");
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(100)) {
        QQmlApplicationEngine errorEngine;
        errorEngine.loadData(R"QML(
            import QtQuick
            import QtQuick.Controls
            ApplicationWindow {
                visible: true
                width: 420
                height: 140
                title: "djconvert"
                Label {
                    anchors.centerIn: parent
                    anchors.margins: 20
                    width: parent.width - 40
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: "djconvert is already running.\nOnly one instance can run at a time."
                }
            }
        )QML");
        return app.exec();
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DjConvertGui", "Main");

    int result = app.exec();

    // A write task is a detached QtConcurrent::run() -- it keeps running
    // on the thread pool independent of any window, so give it a real
    // chance to finish (backup + write are already-fast, small-file
    // operations; a stuck one past this timeout is not worth hanging
    // process exit over) rather than let the process tear down mid-write
    // to a stick.
    QThreadPool::globalInstance()->waitForDone(15000);
    return result;
}
