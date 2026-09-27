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
