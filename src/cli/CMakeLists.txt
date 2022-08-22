qt_add_executable(device-simulator
    main.cpp
    resources.qrc
)

target_link_libraries(device-simulator
    PRIVATE
        DeviceSimulator::Core
        Qt6::Core
        Qt6::Network
)

target_compile_definitions(device-simulator
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
