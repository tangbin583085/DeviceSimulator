function(add_devicesimulator_test target source)
    qt_add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            DeviceSimulator::Core
            Qt6::Test
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_devicesimulator_test(test_protocol test_protocol.cpp)
add_devicesimulator_test(test_server test_server.cpp)
add_devicesimulator_test(test_configuration test_configuration.cpp)
add_devicesimulator_test(test_behavior test_behavior.cpp)
