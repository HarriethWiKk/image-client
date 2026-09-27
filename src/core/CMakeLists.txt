qt_add_library(oic-core STATIC
    oic/core/limits.h
    oic/core/models.h
    oic/core/endpoints.h
    oic/core/urlpolicy.h
    oic/core/decode.h
    oic/core/retrypolicy.h
    models.cpp
    endpoints.cpp
    urlpolicy.cpp
    decode.cpp
    retrypolicy.cpp
)

target_include_directories(oic-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# core must stay free of UI: no Quick, no Qml, no QuickControls2.
# Network is for QHostAddress and the outbound policy, not for any UI stack.
target_link_libraries(oic-core PUBLIC Qt6::Core Qt6::Network)
