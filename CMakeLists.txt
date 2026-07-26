cmake_minimum_required(VERSION 3.21)
project(RoomTunes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network)

qt_standard_project_setup()

if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

add_subdirectory(core)
add_subdirectory(tools/core-cli)

qt_add_executable(RoomTunes
    src/main.cpp
    src/chrome/PlatformChrome.h
    src/chrome/PlatformChrome.cpp
    src/chrome/WindowsChrome.h
    src/chrome/WindowsChrome.cpp
)

target_include_directories(RoomTunes PRIVATE src)

# pragma Singleton inside UiScale.qml isn't enough on its own -- without
# this, qt_add_qml_module registers it as an ordinary (non-singleton)
# type, so a bare "UiScale.factor" reference silently resolves to
# undefined instead of the shared instance.
set_source_files_properties(qml/UiScale.qml qml/BrowseGrid.qml qml/Typography.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(RoomTunes
    URI RoomTunes
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/UiScale.qml
        qml/BrowseGrid.qml
        qml/Typography.qml
        qml/Panel.qml
        qml/ZoneGroupCard.qml
        qml/ZoneGroupList.qml
        qml/ZonesPanel.qml
        qml/ZoneDragGhost.qml
        qml/BusySpinner.qml
        qml/ZoneGroupCompact.qml
        qml/NowPlayingPanel.qml
        qml/NowPlayingWide.qml
        qml/NowPlayingCompact.qml
        qml/NowPlayingArt.qml
        qml/NowPlayingScrubBar.qml
        qml/NowPlayingTransportControls.qml
        qml/NowPlayingVolumeControl.qml
        qml/QueuePanel.qml
        qml/MusicServiceRow.qml
        qml/BrowseHome.qml
        qml/BrowseTile.qml
        qml/RecentlyPlayedPage.qml
        qml/AllServicesPage.qml
        qml/BrowseStack.qml
        qml/BrowseListPage.qml
        qml/SignInDialog.qml
        qml/SearchIconButton.qml
        qml/IconButton.qml
        qml/PillIconButton.qml
        qml/ActionMenu.qml
        qml/TransportIconButton.qml
        qml/TitleBar.qml
        qml/WindowButton.qml
        qml/ResizeBorder.qml
        qml/SlimScrollBar.qml
    RESOURCES
        resources/icons/group.svg
        resources/icons/group_light.svg
        resources/icons/app.png
        resources/icons/skip_previous.svg
        resources/icons/skip_next.svg
        resources/icons/skip_previous_light.svg
        resources/icons/skip_next_light.svg
        resources/icons/search.svg
        resources/icons/chevron_left.svg
        resources/icons/chevron_right.svg
        resources/icons/repeat.svg
        resources/icons/repeat_light.svg
        resources/icons/shuffle.svg
        resources/icons/shuffle_light.svg
        resources/icons/volume_0.svg
        resources/icons/volume_0_light.svg
        resources/icons/volume_1.svg
        resources/icons/volume_1_light.svg
        resources/icons/volume_2.svg
        resources/icons/volume_2_light.svg
        resources/icons/volume_x.svg
        resources/icons/volume_x_light.svg
        resources/icons/play.svg
        resources/icons/play_light.svg
        resources/icons/pause.svg
        resources/icons/pause_light.svg
        resources/icons/roomtunes-logo.svg
        resources/icons/house_pause.svg
        resources/icons/settings.svg
        resources/icons/settings_light.svg
        resources/icons/queue_play.svg
        resources/icons/three_dots.svg
        resources/icons/three_dots_vertical.svg
        resources/icons/library.svg
        resources/icons/unlink.svg
        resources/icons/unlink_light.svg
)

target_link_libraries(RoomTunes PRIVATE Qt6::Quick Qt6::QuickControls2 roomtunes_core)

if(WIN32)
    target_link_libraries(RoomTunes PRIVATE dwmapi)
    # Embeds resources/icons/app.ico as the compiled .exe's own icon --
    # shown in Explorer, taskbar pins, shortcuts, and (since QML has no
    # writable Window.icon property in this Qt build) the running window's
    # taskbar icon too, which Windows derives from the exe's own resource.
    target_sources(RoomTunes PRIVATE resources/app.rc)
endif()

set_target_properties(RoomTunes PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(UNIX AND NOT APPLE)
    set_target_properties(RoomTunes PROPERTIES OUTPUT_NAME roomtunes)
endif()
