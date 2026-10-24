# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 HarriethWiKk
add_library(oic-mcp STATIC
    oic/mcp/server.h
    oic/mcp/tools.h
    server.cpp
    tools.cpp
)

target_include_directories(oic-mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# SPEC 2.1: the MCP host links QtCore + core only. QtQuick/QtQml/QtGui are
# deliberately absent so this binary cannot reach the tray, the single-instance
# lock, or any code path that writes to stdout behind the protocol's back.
target_link_libraries(oic-mcp PUBLIC
    oic-core
    Qt6::Core
)

qt_add_executable(image-client-mcp main.cpp)
target_link_libraries(image-client-mcp PRIVATE oic-mcp)
