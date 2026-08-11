find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(roomtunes_xml_tests
    xml_utils_tests.cpp
)

target_link_libraries(roomtunes_xml_tests PRIVATE Qt6::Test roomtunes_core)

add_test(NAME roomtunes_xml_tests COMMAND roomtunes_xml_tests)

qt_add_executable(roomtunes_aes_crypto_tests
    aes_crypto_tests.cpp
)

target_link_libraries(roomtunes_aes_crypto_tests PRIVATE Qt6::Test roomtunes_core)

add_test(NAME roomtunes_aes_crypto_tests COMMAND roomtunes_aes_crypto_tests)

qt_add_executable(roomtunes_sonos_playback_payload_tests
    sonos_playback_payload_tests.cpp
    support/FakeNetworkAccessManager.h
)

target_include_directories(roomtunes_sonos_playback_payload_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(roomtunes_sonos_playback_payload_tests PRIVATE Qt6::Test roomtunes_core)

add_test(NAME roomtunes_sonos_playback_payload_tests COMMAND roomtunes_sonos_playback_payload_tests)

qt_add_executable(roomtunes_sonos_zone_control_tests
    sonos_zone_control_tests.cpp
    support/FakeNetworkAccessManager.h
)

target_include_directories(roomtunes_sonos_zone_control_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(roomtunes_sonos_zone_control_tests PRIVATE Qt6::Test roomtunes_core)

add_test(NAME roomtunes_sonos_zone_control_tests COMMAND roomtunes_sonos_zone_control_tests)
