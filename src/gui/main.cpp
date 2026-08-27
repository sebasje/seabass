#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QThreadPool>

int main(int argc, char **argv)
{
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
