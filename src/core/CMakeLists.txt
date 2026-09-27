qt_add_library(oic-core STATIC
    oic/core/limits.h
    oic/core/models.h
    oic/core/endpoints.h
    oic/core/urlpolicy.h
    oic/core/decode.h
    oic/core/retrypolicy.h
    oic/net/transport.h
    oic/store/imageprobe.h
    oic/store/paths.h
    oic/store/database.h
    oic/secret/credentials.h
    models.cpp
    endpoints.cpp
    urlpolicy.cpp
    decode.cpp
    retrypolicy.cpp
    net/transport.cpp
    store/imageprobe.cpp
    store/paths.cpp
    store/database.cpp
    secret/credentials.cpp
)

target_include_directories(oic-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# core must stay free of UI: no Quick, no Qml, no QuickControls2.
# Network is for QHostAddress and the outbound policy, not for any UI stack.
target_link_libraries(oic-core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)

# Credential Manager (CredWrite/CredRead/CredDelete/CredEnumerate) lives in
# advapi32. Propagated PUBLIC because oic-core is a static archive: the consumer
# that links it is the one that resolves the import.
if(WIN32)
    target_link_libraries(oic-core PUBLIC advapi32)
endif()
