qt_add_executable(ImageClient WIN32 main.cpp)

qt_add_qml_module(ImageClient
    URI App
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(ImageClient PRIVATE
    oic-core
    oic-app
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
