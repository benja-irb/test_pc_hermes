cmake_minimum_required(VERSION 3.16)
project(hermes-pc LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Common protocol header lives one level up
set(HERMES_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common")

# ---------------------------------------------------------------------------
# Sources — controller implementation is platform-specific
# ---------------------------------------------------------------------------
set(HERMES_SOURCES
    src/main.cpp
    src/udp_client.cpp
)

if(APPLE)
    list(APPEND HERMES_SOURCES src/controller_macos.mm)
else()
    list(APPEND HERMES_SOURCES src/controller.cpp)
endif()

add_executable(hermes-pc ${HERMES_SOURCES})

target_include_directories(hermes-pc PRIVATE
    include
    ${HERMES_COMMON_DIR}
)

# ---------------------------------------------------------------------------
# Platform linking
# ---------------------------------------------------------------------------
if(APPLE)
    # Native GameController.framework — no SDL needed on macOS
    target_link_libraries(hermes-pc
        "-framework GameController"
        "-framework Foundation"
    )
    # ARC is on by default for .mm files; make sure it's enabled
    set_source_files_properties(src/controller_macos.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
else()
    # SDL3 for Linux / Windows
    find_package(SDL3 QUIET)
    if(TARGET SDL3::SDL3)
        target_link_libraries(hermes-pc SDL3::SDL3)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED sdl3)
        target_include_directories(hermes-pc PRIVATE ${SDL3_INCLUDE_DIRS})
        target_link_libraries(hermes-pc ${SDL3_LIBRARIES})
    endif()
endif()

# Windows requires the Winsock runtime library
if(WIN32)
    target_link_libraries(hermes-pc ws2_32)
endif()

target_compile_options(hermes-pc PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# hermes-monitor — passive FeedbackPacket listener (no SDL3, no controller)
# ---------------------------------------------------------------------------
add_executable(hermes-monitor
    src/monitor.cpp
)

target_include_directories(hermes-monitor PRIVATE
    include
    ${HERMES_COMMON_DIR}
)

target_compile_options(hermes-monitor PRIVATE -Wall -Wextra)

if(WIN32)
    target_link_libraries(hermes-monitor ws2_32)
endif()

# ---------------------------------------------------------------------------
# hermes-stick — Thrustmaster TCA Sidestick client
# ---------------------------------------------------------------------------
set(STICK_SOURCES
    src/stick_main.cpp
    src/sidestick_common.cpp
    src/udp_client.cpp
)

if(APPLE)
    # IOKit HID — GameController.framework can't see generic flight sticks
    list(APPEND STICK_SOURCES src/sidestick_macos.cpp)
else()
    list(APPEND STICK_SOURCES src/sidestick.cpp)
endif()

add_executable(hermes-stick ${STICK_SOURCES})

target_include_directories(hermes-stick PRIVATE
    include
    ${HERMES_COMMON_DIR}
)

if(APPLE)
    target_link_libraries(hermes-stick
        "-framework IOKit"
        "-framework CoreFoundation"
    )
elseif(TARGET SDL3::SDL3)
    target_link_libraries(hermes-stick SDL3::SDL3)
else()
    target_include_directories(hermes-stick PRIVATE ${SDL3_INCLUDE_DIRS})
    target_link_libraries(hermes-stick ${SDL3_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(hermes-stick ws2_32)
endif()

target_compile_options(hermes-stick PRIVATE -Wall -Wextra)

option(HERMES_DEBUG "Enable debug trace prints" OFF)
if(HERMES_DEBUG)
    target_compile_definitions(hermes-pc      PRIVATE HERMES_DEBUG)
    target_compile_definitions(hermes-monitor PRIVATE HERMES_DEBUG)
    target_compile_definitions(hermes-stick   PRIVATE HERMES_DEBUG)
endif()
