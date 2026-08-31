cmake_minimum_required(VERSION 3.20)

option(AUDIOCOMPD_FETCH_DEPENDENCIES
    "Download and build third-party dependencies with vcpkg" ON)
option(AUDIOCOMPD_WITH_ALSA "Build the ALSA backend" ON)
option(AUDIOCOMPD_WITH_JACK "Build the JACK backend" ON)
option(AUDIOCOMPD_WITH_PIPEWIRE "Build the PipeWire virtual-sink backend" ON)

if(AUDIOCOMPD_FETCH_DEPENDENCIES)
    include(cmake/BootstrapVcpkg.cmake)
    audiocompd_bootstrap_vcpkg("${CMAKE_CURRENT_LIST_DIR}")

    set(_audiocompd_vcpkg_features "")
    if(AUDIOCOMPD_WITH_ALSA)
        list(APPEND _audiocompd_vcpkg_features alsa)
    endif()
    if(AUDIOCOMPD_WITH_JACK)
        list(APPEND _audiocompd_vcpkg_features jack)
    endif()
    if(AUDIOCOMPD_WITH_PIPEWIRE)
        list(APPEND _audiocompd_vcpkg_features pipewire)
    endif()

    set(VCPKG_MANIFEST_FEATURES "${_audiocompd_vcpkg_features}" CACHE STRING
        "vcpkg features selected from the enabled audiocompd backends" FORCE)
    set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON CACHE BOOL
        "Only install dependencies for enabled audiocompd backends" FORCE)
endif()

project(audiocompd VERSION 0.1.0 LANGUAGES CXX)

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
    if(AUDIOCOMPD_FETCH_DEPENDENCIES)
        set(_audiocompd_vcpkg_installed "${VCPKG_INSTALLED_DIR}")
        if(_audiocompd_vcpkg_installed STREQUAL "")
            set(_audiocompd_vcpkg_installed "${CMAKE_BINARY_DIR}/vcpkg_installed")
        endif()

        file(GLOB _audiocompd_pkgconf_candidates LIST_DIRECTORIES false
            "${_audiocompd_vcpkg_installed}/*/tools/pkgconf/pkgconf")
        if(NOT _audiocompd_pkgconf_candidates)
            message(FATAL_ERROR
                "vcpkg installed pkgconf, but its executable could not be found")
        endif()
        list(GET _audiocompd_pkgconf_candidates 0 _audiocompd_pkgconf)
        set(PKG_CONFIG_EXECUTABLE "${_audiocompd_pkgconf}" CACHE FILEPATH
            "pkgconf executable fetched by vcpkg" FORCE)
    endif()

    find_package(PkgConfig REQUIRED)
endif()

function(audiocompd_add_pkg_dependency target prefix module)
    if(AUDIOCOMPD_FETCH_DEPENDENCIES)
        pkg_check_modules(${prefix} REQUIRED ${module})
        add_library(${target} INTERFACE)
        target_include_directories(${target} SYSTEM INTERFACE
            ${${prefix}_STATIC_INCLUDE_DIRS})
        target_link_directories(${target} INTERFACE
            ${${prefix}_STATIC_LIBRARY_DIRS})
        target_link_libraries(${target} INTERFACE
            ${${prefix}_STATIC_LINK_LIBRARIES})
        target_compile_options(${target} INTERFACE
            ${${prefix}_STATIC_CFLAGS_OTHER})
        target_link_options(${target} INTERFACE
            ${${prefix}_STATIC_LDFLAGS_OTHER})
    else()
        pkg_check_modules(${prefix} REQUIRED IMPORTED_TARGET ${module})
        add_library(${target} ALIAS PkgConfig::${prefix})
    endif()
endfunction()

if(AUDIOCOMPD_WITH_PIPEWIRE)
    audiocompd_add_pkg_dependency(audiocompd_pipewire PIPEWIRE libpipewire-0.3)
    list(APPEND AUDIOCOMPD_SOURCES src/audio/backends/pipewire/PipeWireBackend.cpp)
    list(APPEND AUDIOCOMPD_BACKEND_LIBRARIES audiocompd_pipewire)
endif()

if(AUDIOCOMPD_WITH_ALSA)
    audiocompd_add_pkg_dependency(audiocompd_alsa ALSA alsa)
    list(APPEND AUDIOCOMPD_SOURCES src/audio/backends/alsa/AlsaBackend.cpp)
    list(APPEND AUDIOCOMPD_BACKEND_LIBRARIES audiocompd_alsa)
endif()

if(AUDIOCOMPD_WITH_JACK)
    audiocompd_add_pkg_dependency(audiocompd_jack JACK jack)
    list(APPEND AUDIOCOMPD_SOURCES src/audio/backends/jack/JackBackend.cpp)
    list(APPEND AUDIOCOMPD_BACKEND_LIBRARIES audiocompd_jack)
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

add_executable(audiocompd src/main.cpp ${AUDIOCOMPD_SOURCES})
configure_audiocompd_target(audiocompd)

if(BUILD_TESTING)
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
endif()

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

set(CPACK_PACKAGE_NAME "audiocompd")
set(CPACK_PACKAGE_VENDOR "audiocompd")
set(CPACK_PACKAGE_CONTACT "audiocompd maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Real-time system audio compressor for Linux")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Version used for generated packages")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "audiocompd maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1" CACHE STRING
    "Debian package revision")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6, libgcc-s1, libstdc++6, pipewire")

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_RELEASE "1" CACHE STRING
    "RPM package release")
set(CPACK_RPM_PACKAGE_REQUIRES "pipewire")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_USER_FILELIST
    "%config(noreplace) /etc/audiocompd/audiocompd.xml")

include(CPack)

