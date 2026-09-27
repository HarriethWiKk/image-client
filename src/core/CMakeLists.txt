qt_add_library(oic-core STATIC
    oic/core/limits.h
    oic/core/models.h
    oic/core/endpoints.h
    models.cpp
    endpoints.cpp
)

target_include_directories(oic-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# core must stay free of UI: no Quick, no Qml, no QuickControls2.
target_link_libraries(oic-core PUBLIC Qt6::Core)
