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
option(RPICO_DOCS_DRIVE "Expose the built-in read-only configurator/documentation drive" ON)

if(RPICO_DOCS_DRIVE)
    find_program(RPICO_NODE_EXECUTABLE node)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT RPICO_NODE_EXECUTABLE OR NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "RPICO_DOCS_DRIVE requires Node.js and Python 3; install them or configure with -DRPICO_DOCS_DRIVE=OFF")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configurator/node_modules/esbuild/package.json")
        message(FATAL_ERROR "RPICO_DOCS_DRIVE needs configurator npm dependencies. Run 'cd configurator && npm ci' once, or configure with -DRPICO_DOCS_DRIVE=OFF.")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/configurator/build_disk.py")
        message(FATAL_ERROR "configurator/build_disk.py is required for RPICO_DOCS_DRIVE")
    endif()

    set(RPICO_DOCS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs_drive")
    set(RPICO_DOCS_HTML "${RPICO_DOCS_BUILD_DIR}/index.html")
    set(RPICO_DOCS_IMAGE "${RPICO_DOCS_BUILD_DIR}/rpico-bridge.img")
    file(GLOB_RECURSE RPICO_CONFIGURATOR_INPUTS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/configurator/src/*")

    add_custom_command(
        OUTPUT "${RPICO_DOCS_HTML}"
        COMMAND "${RPICO_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/configurator/build.mjs"
                --out "${RPICO_DOCS_BUILD_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/configurator"
        DEPENDS
            configurator/build.mjs
            configurator/index.template.html
            configurator/package.json
            configurator/package-lock.json
            ${RPICO_CONFIGURATOR_INPUTS}
        VERBATIM
        COMMENT "Bundling the offline configurator"
    )
    add_custom_command(
        OUTPUT "${RPICO_DOCS_IMAGE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/configurator/build_disk.py"
                --app "${RPICO_DOCS_HTML}" --out "${RPICO_DOCS_IMAGE}"
        COMMAND "${CMAKE_COMMAND}" "-DRPICO_DOCS_IMAGE=${RPICO_DOCS_IMAGE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/firmware/verify_docs_image.cmake"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS
            "${RPICO_DOCS_HTML}"
            configurator/build_disk.py
            firmware/verify_docs_image.cmake
            configurator/README.txt
            LICENSE
        VERBATIM
        COMMENT "Creating the read-only FAT12 documentation image"
    )
    add_custom_target(rpico_docs_drive_image DEPENDS "${RPICO_DOCS_IMAGE}")
    set(RPICO_DOCS_ASSEMBLY "${CMAKE_CURRENT_BINARY_DIR}/docs_drive_image.S")
    file(WRITE "${RPICO_DOCS_ASSEMBLY}"
        ".section .rodata.rpico_docs_image,\"a\",%progbits\n"
        ".balign 4\n.global rpico_docs_image\n.type rpico_docs_image,%object\nrpico_docs_image:\n"
        ".incbin \"${RPICO_DOCS_IMAGE}\"\n"
        ".global rpico_docs_image_end\nrpico_docs_image_end:\n"
        ".size rpico_docs_image,rpico_docs_image_end-rpico_docs_image\n")
    set_source_files_properties("${RPICO_DOCS_ASSEMBLY}" PROPERTIES
        OBJECT_DEPENDS "${RPICO_DOCS_IMAGE}")
endif()

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
    firmware/led_config.c
    firmware/usb_device.c
    firmware/usb_host.c
    firmware/cdc_bench.c
)
if(NOT RPICO_CDC_BENCH)
    list(APPEND bridge_sources firmware/usb_host_hid.c firmware/usb_host_identity.c)
endif()
if(RPICO_DOCS_DRIVE)
    list(APPEND bridge_sources firmware/usb_docs_msc.c)
endif()
set(littlefs_sources
    firmware/third_party/littlefs/lfs.c
    firmware/third_party/littlefs/lfs_util.c
)
if(RPICO_DOCS_DRIVE)
    add_executable(rpico_bridge ${bridge_sources} ${littlefs_sources}
        firmware/embedded_cli.c "${RPICO_DOCS_ASSEMBLY}")
else()
    add_executable(rpico_bridge ${bridge_sources} ${littlefs_sources} firmware/embedded_cli.c)
endif()
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
    LFS_NO_MALLOC=1
    LFS_NO_DEBUG=1
    LFS_NO_WARN=1
    LFS_NO_ERROR=1
)
if(RPICO_DOCS_DRIVE)
    target_compile_definitions(rpico_bridge PRIVATE RPICO_DOCS_DRIVE=1)
    add_dependencies(rpico_bridge rpico_docs_drive_image)
endif()
target_include_directories(rpico_bridge PRIVATE firmware/third_party/littlefs)
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
