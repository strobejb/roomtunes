find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_executable(core-cli
    main.cpp
)

target_link_libraries(core-cli PRIVATE roomtunes_core)
