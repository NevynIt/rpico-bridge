cmake_minimum_required(VERSION 3.16)
project(rpico_bridge_native_tests C)

enable_testing()

add_executable(midi_router_test
    midi_router_test.c
    ../firmware/midi_router.c
    ../firmware/route_policy.c
)
target_include_directories(midi_router_test PRIVATE ../firmware)
target_compile_features(midi_router_test PRIVATE c_std_11)
target_compile_options(midi_router_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME midi_router_test COMMAND midi_router_test)

add_executable(bridge_integration_test
    bridge_integration_test.c
    usb_bridge_mock.c
    ../firmware/bridge.c
    ../firmware/midi_router.c
    ../firmware/route_policy.c
)
target_include_directories(bridge_integration_test PRIVATE ../firmware .)
target_compile_features(bridge_integration_test PRIVATE c_std_11)
target_compile_options(bridge_integration_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME bridge_integration_test COMMAND bridge_integration_test)
