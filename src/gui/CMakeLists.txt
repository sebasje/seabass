qt_add_executable(seabass
    WIN32
    main.cpp
)
if(WIN32)
    # App icon (.exe icon + taskbar/Alt-Tab), generated from the same
    # seabass_soundbass.svg the QML UI itself uses. Regenerate with:
    #   magick -background none qml/icons/seabass_soundbass.svg \
    #       -define icon:auto-resize=256,128,64,48,32,16 win/app_icon.ico
    target_sources(seabass PRIVATE win/app_icon.rc)
endif()

# `pragma Singleton` alone isn't enough for this Qt version's
# qt_add_qml_module to register Theme.qml as an actual QML singleton.
# Without this, the generated qmldir lists it as a plain component type
# ("Theme 1.0 qml/Theme.qml" instead of "singleton Theme 1.0 ..."), so
# every `Theme.xxx` property access anywhere in the app silently
# evaluates to `undefined` at runtime (QML logs "Unable to assign
# [undefined] to ..." rather than a hard error) instead of throwing a
# clear "Theme is not a type" error, verified via the generated qmldir
# and confirmed live via journalctl. Must be set *before*
# qt_add_qml_module() is called. It reads source file properties at
# call time, not deferred, so setting this afterward has no effect
# (confirmed: moving it after here silently produced the same broken
# non-singleton qmldir entry even after a full clean rebuild).
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(seabass
    URI SeabassGui
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/StickListPage.qml
        qml/ActionCard.qml
        qml/DuplicatesHubPage.qml
        qml/BackupsHubPage.qml
        qml/ScanPage.qml
        qml/DuplicatesPage.qml
        qml/CleanupPage.qml
        qml/PendingDeletionsPage.qml
        qml/JunkCuePage.qml
        qml/LibraryConsistencyPage.qml
        qml/StickStatisticsPage.qml
        qml/TreemapView.qml
        qml/EngineLibraryCreatorPage.qml
        qml/SyncPage.qml
        qml/BackupsPage.qml
        qml/LocalCuePage.qml
        qml/FormatToggle.qml
        qml/LibrarySourceToggle.qml
        qml/KeyBadge.qml
        qml/AboutPage.qml
        qml/PlayerBar.qml
        qml/WaveformView.qml
        qml/SettingsPage.qml
        qml/AppSettingsPage.qml
        qml/AnonymizeLibraryPage.qml
        qml/InfoButton.qml
        qml/UsbStickIcon.qml
        qml/StickWriteWarning.qml
        qml/RekordboxRunningWarning.qml
        qml/CueFallbackNotice.qml
        qml/TrackWaveformCard.qml
        qml/BigScrollBar.qml
        qml/BusyOverlay.qml
    RESOURCES
        qml/icons/seabass_soundbass.svg
    SOURCES
        media_controller.hpp
        media_controller.cpp
        scan_controller.hpp
        scan_controller.cpp
        duplicates_controller.hpp
        duplicates_controller.cpp
        cleanup_controller.hpp
        cleanup_controller.cpp
        add_cue_controller.hpp
        add_cue_controller.cpp
        library_consistency_controller.hpp
        library_consistency_controller.cpp
        stick_statistics_controller.hpp
        stick_statistics_controller.cpp
        engine_library_creator_controller.hpp
        engine_library_creator_controller.cpp
        sync_controller.hpp
        sync_controller.cpp
        backups_controller.hpp
        backups_controller.cpp
        local_cue_controller.hpp
        local_cue_controller.cpp
        playback_controller.hpp
        playback_controller.cpp
        settings_controller.hpp
        settings_controller.cpp
        app_settings_controller.hpp
        app_settings_controller.cpp
        anonymize_library_controller.hpp
        anonymize_library_controller.cpp
        rekordbox_guard_controller.hpp
        rekordbox_guard_controller.cpp
        write_guard.hpp
        write_guard.cpp
        qt_progress_reporter.hpp
)

target_include_directories(seabass PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(seabass PRIVATE
    seabass_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickLayouts
    Qt6::QuickEffects
    Qt6::QuickDialogs2
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::Svg
)
