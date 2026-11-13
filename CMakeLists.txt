cmake_minimum_required(VERSION 3.20)
project(PeripheralTelemetryDaemon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(nlohmann_json REQUIRED)
find_package(httplib REQUIRED)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

add_library(daemon_core STATIC
    src/MouseDevice.cpp
    src/HeadsetDevice.cpp
    src/DeviceManager.cpp
    src/TelemetrySyncClient.cpp
    src/ControlServer.cpp
)
target_include_directories(daemon_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(daemon_core PUBLIC
    nlohmann_json::nlohmann_json
    httplib::httplib
    Threads::Threads
)

add_executable(peripheral_daemon src/main.cpp)
target_link_libraries(peripheral_daemon PRIVATE daemon_core)
