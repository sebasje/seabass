qt_add_executable(djconvert-gui
    main.cpp
)

qt_add_qml_module(djconvert-gui
    URI DjConvertGui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/StickListPage.qml
        qml/ScanPage.qml
        qml/DuplicatesPage.qml
        qml/SyncPage.qml
        qml/BackupsPage.qml
        qml/LocalCuePage.qml
        qml/FormatToggle.qml
        qml/AboutPage.qml
        qml/PlayerBar.qml
        qml/WaveformView.qml
        qml/SettingsPage.qml
        qml/AppSettingsPage.qml
        qml/UsbStickIcon.qml
    RESOURCES
        qml/icons/seabass_soundbass.svg
    SOURCES
        media_controller.hpp
        media_controller.cpp
        scan_controller.hpp
        scan_controller.cpp
        duplicates_controller.hpp
        duplicates_controller.cpp
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
        qt_progress_reporter.hpp
)

target_include_directories(djconvert-gui PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(djconvert-gui PRIVATE
    djconvert_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickLayouts
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::Svg
)
