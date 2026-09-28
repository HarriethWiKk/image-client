qt_add_library(oic-app STATIC
    oic/app/backend.h
    oic/app/assetimageprovider.h
    oic/app/historymodel.h
    oic/app/profilecontroller.h
    oic/app/jobcontroller.h
    oic/app/settingscontroller.h
    oic/app/lightboxcontroller.h
    backend.cpp
    assetimageprovider.cpp
    historymodel.cpp
    profilecontroller.cpp
    jobcontroller.cpp
    settingscontroller.cpp
    lightboxcontroller.cpp
)

target_include_directories(oic-app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# gui-side library: may use QtGui/Quick (image decoding, QML types), unlike the Gui-free
# oic-core. PUBLIC because ImageClient and the gui-side tests both link oic-app.
target_link_libraries(oic-app PUBLIC
    oic-core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
