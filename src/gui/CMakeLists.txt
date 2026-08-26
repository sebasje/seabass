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
        qml/PlayerBar.qml
        qml/WaveformView.qml
    SOURCES
        media_controller.hpp
        media_controller.cpp
        scan_controller.hpp
        scan_controller.cpp
        duplicates_controller.hpp
        duplicates_controller.cpp
        playback_controller.hpp
        playback_controller.cpp
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
)
