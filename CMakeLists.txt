cmake_minimum_required(VERSION 3.17)

set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Target Pico SDK board")
if(NOT PICO_BOARD STREQUAL "waveshare_rp2040_zero")
    message(FATAL_ERROR "rpico-bridge targets waveshare_rp2040_zero")
endif()

if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "Set PICO_SDK_PATH to the Pico SDK 2.2.0 checkout")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(rpico_bridge LANGUAGES C CXX ASM)
if(NOT PICO_SDK_VERSION_STRING STREQUAL "2.2.0")
    message(FATAL_ERROR "rpico-bridge requires Pico SDK 2.2.0")
endif()

if(NOT PICO_TINYUSB_PATH)
    set(PICO_TINYUSB_PATH "$ENV{PICO_TINYUSB_PATH}")
endif()
if(NOT EXISTS "${PICO_TINYUSB_PATH}/src/tusb_option.h")
    message(FATAL_ERROR "Set PICO_TINYUSB_PATH to TinyUSB 0.21.0 or rebuild the dev container")
endif()

option(RPICO_CDC_BENCH "Disable PIO USB MIDI/HID host for native-only bench testing" OFF)

if(NOT PICO_PIO_USB_PATH)
    set(PICO_PIO_USB_PATH "$ENV{PICO_PIO_USB_PATH}")
endif()
pico_sdk_init()
if(NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "Set PICO_PIO_USB_PATH to Pico-PIO-USB 0.7.3")
endif()

set(bridge_sources
    firmware/main.c
    firmware/bridge.c
    firmware/midi_router.c
    firmware/route_policy.c
    firmware/route_control.c
    firmware/route_store.c
    firmware/usb_device.c
    firmware/usb_host.c
    firmware/cdc_bench.c
)
if(NOT RPICO_CDC_BENCH)
    list(APPEND bridge_sources firmware/usb_host_hid.c firmware/usb_host_identity.c)
endif()
add_executable(rpico_bridge ${bridge_sources} firmware/embedded_cli.c)
target_compile_features(rpico_bridge PRIVATE c_std_11)
set_source_files_properties(${bridge_sources} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wstrict-prototypes"
)
target_include_directories(rpico_bridge PRIVATE firmware)
# This firmware runs only core 0 (PIO-USB is foreground-polled), so Pico SDK
# flash_safe_execute may treat the never-launched core 1 as safe.
target_compile_definitions(rpico_bridge PRIVATE
    PICO_NO_BI_PROGRAM_BUILD_DATE=1
    PICO_FLASH_ASSUME_CORE1_SAFE=1
)
target_link_libraries(rpico_bridge PRIVATE pico_stdlib pico_status_led pico_unique_id pico_bootrom pico_flash tinyusb_device)
if(RPICO_CDC_BENCH)
    target_compile_definitions(rpico_bridge PRIVATE RPICO_CDC_BENCH=1)
else()
    target_link_libraries(rpico_bridge PRIVATE tinyusb_host tinyusb_pico_pio_usb)
endif()
pico_enable_stdio_uart(rpico_bridge 0)
pico_enable_stdio_usb(rpico_bridge 0)
pico_set_program_name(rpico_bridge "rpico-bridge")
pico_set_program_description(rpico_bridge "USB MIDI bridge for RP2040-Zero")
pico_add_extra_outputs(rpico_bridge)
