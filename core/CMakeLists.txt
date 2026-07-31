find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

qt_add_library(roomtunes_core STATIC
    Logging.h
    Logging.cpp
    settings/Settings.h
    settings/Settings.cpp
    settings/BrowseHistoryStore.h
    settings/BrowseHistoryStore.cpp

    xml/XmlUtils.h

    crypto/Aes128Cbc.h
    crypto/Aes128Cbc.cpp

    upnp/UpnpService.h
    upnp/Soap.h
    upnp/SoapResponse.h
    upnp/SoapResponse.cpp
    upnp/UpnpServiceBase.h
    upnp/Ssdp.h
    upnp/Ssdp.cpp
    upnp/GenaNotifyServer.h
    upnp/GenaNotifyServer.cpp
    upnp/Didl.h
    upnp/Didl.cpp
    upnp/Smapi.h
    upnp/services/AudioIn.h
    upnp/services/AVTransport.h
    upnp/services/RenderingControl.h
    upnp/services/ZoneGroupTopology.h
    upnp/services/ContentDirectory.h
    upnp/services/DeviceProperties.h
    upnp/services/MusicServices.h
    upnp/services/SystemProperties.h

    media/MediaItem.h
    media/AlbumColorAnalyzer.h
    media/AlbumColorAnalyzer.cpp

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
    zone/ZoneDiscovery.h
    zone/ZoneDiscovery.cpp
    zone/NetworkWatcher.h
    zone/NetworkWatcher.cpp
    zone/Household.h
    zone/Household.cpp

    services/MusicService.h
    services/MusicService.cpp
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
