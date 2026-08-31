cmake_minimum_required(VERSION 3.20)

project(audiocompd VERSION 0.1.0 LANGUAGES CXX)

option(AUDIOCOMPD_WITH_ALSA "Build the ALSA backend" ON)
option(AUDIOCOMPD_WITH_JACK "Build the JACK backend" ON)
option(AUDIOCOMPD_WITH_PIPEWIRE "Build the PipeWire virtual-sink backend" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest)
include(cmake/CompilerWarnings.cmake)

find_package(Threads REQUIRED)
find_package(LibXml2 REQUIRED)

set(AUDIOCOMPD_SOURCES
    src/application/Application.cpp
    src/audio/AudioBackendFactory.cpp
    src/audio/AudioEngine.cpp
    src/compressor/Compressor.cpp
    src/compressor/EnvelopeFollower.cpp
    src/config/Config.cpp
    src/logging/Logger.cpp
    src/runtime/SignalHandler.cpp
)

set(AUDIOCOMPD_BACKEND_LIBRARIES "")

if(AUDIOCOMPD_WITH_ALSA OR AUDIOCOMPD_WITH_JACK OR AUDIOCOMPD_WITH_PIPEWIRE)
    find_package(PkgConfig REQUIRED)
endif()

if(AUDIOCOMPD_WITH_PIPEWIRE)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    list(APPEND AUDIOCOMPD_SOURCES src/audio/backends/pipewire/PipeWireBackend.cpp)
    list(APPEND AUDIOCOMPD_BACKEND_LIBRARIES PkgConfig::PIPEWIRE)
endif()

if(AUDIOCOMPD_WITH_ALSA)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
    list(APPEND AUDIOCOMPD_SOURCES src/audio/backends/alsa/AlsaBackend.cpp)
    list(APPEND AUDIOCOMPD_BACKEND_LIBRARIES PkgConfig::ALSA)
endif()

if(AUDIOCOMPD_WITH_JACK)
    pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
    list(APPEND AUDIOCOMPD_SOURCES src/audio/backends/jack/JackBackend.cpp)
    list(APPEND AUDIOCOMPD_BACKEND_LIBRARIES PkgConfig::JACK)
endif()

function(configure_audiocompd_target target)
    target_include_directories(${target} PRIVATE src)
    target_link_libraries(${target} PRIVATE
        Threads::Threads
        LibXml2::LibXml2
        ${AUDIOCOMPD_BACKEND_LIBRARIES}
    )
    target_compile_definitions(${target} PRIVATE
        AUDIOCOMPD_VERSION="${PROJECT_VERSION}"
        $<$<BOOL:${AUDIOCOMPD_WITH_ALSA}>:AUDIOCOMPD_WITH_ALSA>
        $<$<BOOL:${AUDIOCOMPD_WITH_JACK}>:AUDIOCOMPD_WITH_JACK>
        $<$<BOOL:${AUDIOCOMPD_WITH_PIPEWIRE}>:AUDIOCOMPD_WITH_PIPEWIRE>
    )
    audiocompd_enable_warnings(${target})
endfunction()

# The project intentionally has exactly two executable targets.
add_executable(audiocompd src/main.cpp ${AUDIOCOMPD_SOURCES})
configure_audiocompd_target(audiocompd)

add_executable(unit_test
    tests/main.cpp
    tests/audio/AudioEngineTest.cpp
    tests/compressor/CompressorTest.cpp
    tests/compressor/EnvelopeFollowerTest.cpp
    tests/config/ConfigTest.cpp
    tests/logging/LoggerTest.cpp
    ${AUDIOCOMPD_SOURCES}
)
configure_audiocompd_target(unit_test)
target_include_directories(unit_test PRIVATE tests)
target_compile_definitions(unit_test PRIVATE
    AUDIOCOMPD_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME unit_test COMMAND unit_test)

install(TARGETS audiocompd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(AUDIOCOMPD_CONFIG_INSTALL_DIR "/etc/audiocompd" CACHE PATH
    "Installation directory for the audiocompd configuration")
set(AUDIOCOMPD_SCHEMA_INSTALL_DIR "/usr/share/audiocompd" CACHE PATH
    "Installation directory for the audiocompd XSD")
set(AUDIOCOMPD_SYSTEMD_INSTALL_DIR "/usr/lib/systemd/user" CACHE PATH
    "Installation directory for the systemd user unit")

install(FILES config/audiocompd.xml DESTINATION ${AUDIOCOMPD_CONFIG_INSTALL_DIR})
install(FILES schema/audiocompd.xsd DESTINATION ${AUDIOCOMPD_SCHEMA_INSTALL_DIR})
install(FILES packaging/systemd/audiocompd.service
    DESTINATION ${AUDIOCOMPD_SYSTEMD_INSTALL_DIR})
