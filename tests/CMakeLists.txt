get_target_property(OIC_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(OIC_QT_BIN_DIR "${OIC_QMAKE_EXECUTABLE}" DIRECTORY)

# Qt is not installed system-wide on this machine, so test processes need the Qt
# bin directory on PATH to find Qt6Test.dll / Qt6Core.dll. $ENV{PATH} is expanded
# at configure time; re-run configure after changing the shell PATH.
function(oic_add_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test oic-core)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${OIC_QT_BIN_DIR};$ENV{PATH}")
endfunction()

oic_add_test(tst_endpoints tst_endpoints.cpp)
oic_add_test(tst_models tst_models.cpp)
oic_add_test(tst_urlpolicy tst_urlpolicy.cpp)
oic_add_test(tst_decode tst_decode.cpp)
oic_add_test(tst_retrypolicy tst_retrypolicy.cpp)
oic_add_test(tst_transport tst_transport.cpp)

# Only the image probe test needs QtGui: it cross-checks the header parser
# against Qt's own decoder. The library itself stays Gui-free.
oic_add_test(tst_imageprobe tst_imageprobe.cpp)
target_link_libraries(tst_imageprobe PRIVATE Qt6::Gui)

oic_add_test(tst_store tst_store.cpp)
oic_add_test(tst_assets tst_assets.cpp)
oic_add_test(tst_protocol tst_protocol.cpp)
oic_add_test(tst_parsers tst_parsers.cpp)
# Fixtures are real encoded images, which needs the GUI codecs.
target_link_libraries(tst_protocol PRIVATE Qt6::Gui)

# jobs orchestrates the core over injected seams; GUI-less (fixtures are header-only
# PNG byte constants, decoded by the store probe rather than QtGui).
oic_add_test(tst_jobs tst_jobs.cpp)

# The bounded pool: real worker QThreads, queued signals, fail-fast submit, cancel, TTL.
oic_add_test(tst_jobmanager tst_jobmanager.cpp)

# jobs <-> real net::Transport over loopback: proves the seam does not bypass SSRF/pinning.
oic_add_test(tst_jobs_integration tst_jobs_integration.cpp)

# gui-side oic-app foundation (Backend + AssetImageProvider); links oic-app, which pulls
# Qt Gui/Quick for QImage/QImageReader.
qt_add_executable(tst_app tst_app.cpp)
target_link_libraries(tst_app PRIVATE Qt6::Test oic-app)
add_test(NAME tst_app COMMAND tst_app)
set_tests_properties(tst_app PROPERTIES ENVIRONMENT "PATH=${OIC_QT_BIN_DIR};$ENV{PATH}")

# Reference-intake view binding, proven offscreen (no manual smoke). Needs Qt Quick for the
# engine + Repeater that this test drives.
qt_add_executable(tst_guiqml tst_guiqml.cpp)
target_link_libraries(tst_guiqml PRIVATE Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick oic-app)
add_test(NAME tst_guiqml COMMAND tst_guiqml)
set_tests_properties(tst_guiqml PROPERTIES ENVIRONMENT "PATH=${OIC_QT_BIN_DIR};$ENV{PATH}")

# Talks to the real Windows Credential Manager, so it is Windows-only.
if(WIN32)
    oic_add_test(tst_credentials tst_credentials.cpp)
endif()
