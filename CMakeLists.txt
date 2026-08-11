cmake_minimum_required(VERSION 3.21)

set(ROOMTUNES_VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/app/version.h")
file(STRINGS "${ROOMTUNES_VERSION_HEADER}" ROOMTUNES_VERSION_LINES
    REGEX "^#define VERSION_(MAJOR|MINOR|PATCH|BUILD_COUNT)[ \t]+[0-9]+")
foreach(_version_line IN LISTS ROOMTUNES_VERSION_LINES)
    if(_version_line MATCHES "^#define VERSION_([A-Z_]+)[ \t]+([0-9]+)")
        set(ROOMTUNES_VERSION_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()
foreach(_version_part MAJOR MINOR PATCH BUILD_COUNT)
    if(NOT DEFINED ROOMTUNES_VERSION_${_version_part})
        message(FATAL_ERROR "Missing VERSION_${_version_part} in ${ROOMTUNES_VERSION_HEADER}")
    endif()
endforeach()

project(RoomTunes
    VERSION ${ROOMTUNES_VERSION_MAJOR}.${ROOMTUNES_VERSION_MINOR}.${ROOMTUNES_VERSION_PATCH}
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2 Svg Test Xml)

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(tools/core-cli)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
