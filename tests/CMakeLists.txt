find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(roomtunes_xml_tests
    xml_utils_tests.cpp
)

target_link_libraries(roomtunes_xml_tests PRIVATE Qt6::Test roomtunes_core)

add_test(NAME roomtunes_xml_tests COMMAND roomtunes_xml_tests)
