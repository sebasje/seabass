#include "engine_library_creator_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>

#include "application/use_cases/scan_library.hpp"
#include "infrastructure/engine/libdjinterop_engine_library_creator.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

namespace djconvert::gui
{

namespace fs = std::filesystem;
using infrastructure::engine::EngineLibraryCreator;
using infrastructure::engine::EngineSchemaGeneration;

namespace
{

EngineSchemaGeneration schemaFromInt(int value)
{
    switch (value) {
    case 0: return EngineSchemaGeneration::V1;
    case 2: return EngineSchemaGeneration::V3;
    default: return EngineSchemaGeneration::V2;
    }
}

// Runs entirely on a background thread (see
// EngineLibraryCreatorController::create()) -- no access to the
// controller itself.
EngineLibraryCreationTaskResult runCreateTask(QString rekordboxPath, int schemaGeneration,
                                               std::shared_ptr<QtProgressReporter> reporter)
{
    EngineLibraryCreationTaskResult result;
    try {
        infrastructure::rekordbox::KaitaiRekordboxReader reader(rekordboxPath.toStdString());
        reader.setProgressReporter(*reporter);
        auto tracks = application::ScanLibrary(reader).execute();

        std::string engineLibraryPath =
            (fs::path(rekordboxPath.toStdString()).parent_path() / "Engine Library").string();
        auto creation = EngineLibraryCreator::create(engineLibraryPath, tracks, schemaFromInt(schemaGeneration));

        result.tracksCreated = creation.tracksCreated;
        result.tracksSkipped = creation.tracksSkipped;
        result.cuesCopied = creation.cuesCopied;
        if (!creation.errorMessage.empty()) {
            result.errorMessage = QString::fromStdString(creation.errorMessage);
        }
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

EngineLibraryCreatorController::EngineLibraryCreatorController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<EngineLibraryCreationTaskResult>::finished, this,
            &EngineLibraryCreatorController::onCreateFinished);
}

std::shared_ptr<QtProgressReporter> EngineLibraryCreatorController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

void EngineLibraryCreatorController::create(const QString &rekordboxPath, int schemaGeneration)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    m_watcher.setFuture(QtConcurrent::run(runCreateTask, rekordboxPath, schemaGeneration, makeReporter()));
}

void EngineLibraryCreatorController::onCreateFinished()
{
    EngineLibraryCreationTaskResult result = m_watcher.result();
    setBusy(false);
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        return;
    }
    setStatusMessage(QString("Created %1 track(s) (%2 skipped, no local file), copied %3 cue(s).")
                          .arg(result.tracksCreated)
                          .arg(result.tracksSkipped)
                          .arg(result.cuesCopied));
}

void EngineLibraryCreatorController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void EngineLibraryCreatorController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void EngineLibraryCreatorController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void EngineLibraryCreatorController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace djconvert::gui
