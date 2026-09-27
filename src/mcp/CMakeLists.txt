qt_add_executable(image-client-mcp main.cpp)

# SPEC 2.1: the MCP host links QtCore + core only. QtQuick/QtQml/QtGui are
# deliberately absent so this binary cannot reach the tray, the single-instance
# lock, or any code path that writes to stdout behind the protocol's back.
target_link_libraries(image-client-mcp PRIVATE
    oic-core
    Qt6::Core
)
