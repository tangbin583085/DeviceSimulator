add_library(DeviceSimulatorCore STATIC)

target_sources(DeviceSimulatorCore
    PRIVATE
        src/Configuration.cpp
        src/DemoBinaryProtocolCodec.cpp
        src/DeviceSimulatorServer.cpp
        include/DeviceSimulator/Configuration.h
        include/DeviceSimulator/DemoBinaryProtocolCodec.h
        include/DeviceSimulator/DeviceSimulatorServer.h
        include/DeviceSimulator/Protocol.h
        include/DeviceSimulator/Rules.h
)

target_include_directories(DeviceSimulatorCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(DeviceSimulatorCore
    PUBLIC
        Qt6::Core
        Qt6::Network
)

target_compile_definitions(DeviceSimulatorCore
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

add_library(DeviceSimulator::Core ALIAS DeviceSimulatorCore)
