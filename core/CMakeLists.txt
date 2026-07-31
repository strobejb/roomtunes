find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

qt_add_library(roomtunes_core STATIC
    Logging.h
    Logging.cpp
    settings/Settings.h
    settings/Settings.cpp
    settings/AppSettings.h
    settings/AppSettings.cpp
    settings/BrowseHistoryStore.h
    settings/BrowseHistoryStore.cpp

    xml/XmlUtils.h

    crypto/Aes128Cbc.h
    crypto/Aes128Cbc.cpp

    control/UpnpService.h
    control/Soap.h
    control/SoapResponse.h
    control/SoapResponse.cpp
    control/UpnpServiceBase.h
    control/Didl.h
    control/Didl.cpp
    control/SonosPlaybackPayload.h
    control/SonosPlaybackPayload.cpp
    control/SonosZoneControl.h
    control/SonosZoneControl.cpp
    control/services/AudioIn.h
    control/services/AVTransport.h
    control/services/RenderingControl.h
    control/services/ZoneGroupTopology.h
    control/services/ContentDirectory.h
    control/services/DeviceProperties.h
    control/services/MusicServices.h
    control/services/Queue.h
    control/services/SystemProperties.h

    media/MediaItem.h
    media/AlbumColorAnalyzer.h
    media/AlbumColorAnalyzer.cpp

    discovery/Ssdp.h
    discovery/Ssdp.cpp
    discovery/ZoneDiscovery.h
    discovery/ZoneDiscovery.cpp
    discovery/NetworkWatcher.h
    discovery/NetworkWatcher.cpp

    eventing/GenaNotifyServer.h
    eventing/GenaNotifyServer.cpp
    eventing/ZoneEventing.h
    eventing/ZoneEventing.cpp

    models/GroupedZoneModel.h
    models/GroupedZoneModel.cpp
    models/QueueModel.h
    models/QueueModel.cpp
    models/MusicServiceListModel.h
    models/MusicServiceListModel.cpp
    models/RecentlyPlayedModel.h
    models/RecentlyPlayedModel.cpp

    zone/ZonePlayer.h
    zone/ZonePlayer.cpp
    zone/Household.h
    zone/Household.cpp

    services/MusicService.h
    services/MusicService.cpp
    services/Smapi.h
    services/MusicServiceCatalog.h
    services/MusicServiceCatalog.cpp
    services/ServiceLogoCatalog.h
    services/ServiceLogoCatalog.cpp
    services/SmapiService.h
    services/SmapiService.cpp
    services/SonosLibraryService.h
    services/SonosLibraryService.cpp
    services/SpotifyServiceConfig.h
    services/ThirdPartyMediaServers.h
    services/ThirdPartyMediaServers.cpp
)

target_include_directories(roomtunes_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(roomtunes_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
