find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Svg)

qt_standard_project_setup()

if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(RoomTunes
    main.cpp
    version.h
    ui/RoundedImage.h
    ui/RoundedImage.cpp
    desktop/chrome/PlatformChrome.h
    desktop/chrome/PlatformChrome.cpp
    desktop/chrome/WindowsChrome.h
    desktop/chrome/WindowsChrome.cpp
)

target_include_directories(RoomTunes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# pragma Singleton inside UiScale.qml isn't enough on its own -- without
# this, qt_add_qml_module registers it as an ordinary (non-singleton)
# type, so a bare "UiScale.factor" reference silently resolves to
# undefined instead of the shared instance.
set_source_files_properties(qml/UiScale.qml qml/BrowseGrid.qml qml/Typography.qml qml/VolumeIcon.qml
    qml/ActionMenuState.qml PROPERTIES
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
        qml/VolumeIcon.qml
        qml/ActionMenuState.qml
        qml/Panel.qml
        qml/EqualizerIcon.qml
        qml/ScrollingText.qml
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
        qml/NowPlayingActionStrip.qml
        qml/NowPlayingContextMenu.qml
        qml/ZoneVolumeControl.qml
        qml/QueuePanel.qml
        qml/NewPlaylistDialog.qml
        qml/QueueCompact.qml
        qml/MusicServiceRow.qml
        qml/BrowseHome.qml
        qml/BrowseTile.qml
        qml/RecentlyPlayedPage.qml
        qml/AllServicesPage.qml
        qml/BrowseStack.qml
        qml/BrowseListPage.qml
        qml/BrowseHomeSearchPage.qml
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
        resources/icons/heart.svg
        resources/icons/heart_light.svg
        resources/icons/heart_outline.svg
        resources/icons/heart_outline_light.svg
        resources/icons/shuffle.svg
        resources/icons/shuffle_light.svg
        resources/icons/volume_0.svg
        resources/icons/volume_0_light.svg
        resources/icons/volume_1.svg
        resources/icons/volume_1_light.svg
        resources/icons/volume_2.svg
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
        resources/icons/drag_handle.svg
        resources/icons/three_dots.svg
        resources/icons/three_dots_light.svg
        resources/icons/three_dots_vertical.svg
        resources/icons/library.svg
        resources/icons/line_in.svg
        resources/icons/tv.svg
        resources/icons/unlink.svg
        resources/icons/unlink_light.svg
        resources/icons/triangle_down.svg
        resources/icons/triangle_down_light.svg
        resources/icons/triangle_up.svg
        resources/icons/triangle_up_light.svg
        resources/icons/window_close_symbolic.svg
        resources/icons/window_maximize_symbolic.svg
        resources/icons/window_minimize_symbolic.svg
        resources/icons/window_restore_symbolic.svg
)

target_link_libraries(RoomTunes PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg roomtunes_core)

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
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(UNIX AND NOT APPLE)
    set_target_properties(RoomTunes PROPERTIES OUTPUT_NAME roomtunes)

    include(GNUInstallDirs)
    install(FILES resources/roomtunes.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES resources/roomtunes.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
    install(FILES resources/icons/app.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME roomtunes.png
    )
endif()
