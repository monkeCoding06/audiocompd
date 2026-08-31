set(AUDIOCOMPD_VCPKG_COMMIT
    "9e593bb18ea69cc5095e012465dcd675a822ed0d")

function(_audiocompd_read_vcpkg_metadata metadata_file variable output_variable)
    file(STRINGS "${metadata_file}" metadata_line
        REGEX "^${variable}=")
    if(NOT metadata_line)
        message(FATAL_ERROR
            "The downloaded vcpkg metadata does not define ${variable}")
    endif()

    list(GET metadata_line 0 metadata_line)
    string(REGEX REPLACE "^[^=]+=" "" metadata_value "${metadata_line}")
    set(${output_variable} "${metadata_value}" PARENT_SCOPE)
endfunction()

function(audiocompd_bootstrap_vcpkg source_directory)
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR
            "audiocompd and its automatically fetched audio backends require Linux")
    endif()

    set(vcpkg_download_directory "${CMAKE_BINARY_DIR}/_deps/downloads")
    set(vcpkg_root "${CMAKE_BINARY_DIR}/_deps/vcpkg")
    set(vcpkg_toolchain "${vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
    set(vcpkg_executable "${vcpkg_root}/vcpkg")
    set(vcpkg_archive
        "${vcpkg_download_directory}/vcpkg-${AUDIOCOMPD_VCPKG_COMMIT}.tar.gz")
    set(vcpkg_extracted_directory
        "${CMAKE_BINARY_DIR}/_deps/vcpkg-${AUDIOCOMPD_VCPKG_COMMIT}")

    if(DEFINED CMAKE_TOOLCHAIN_FILE
       AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL ""
       AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "${vcpkg_toolchain}")
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE FILEPATH
            "Caller-provided toolchain chained through vcpkg" FORCE)
        message(STATUS
            "Chaining caller-provided toolchain through vcpkg: ${CMAKE_TOOLCHAIN_FILE}")
    endif()

    if(NOT EXISTS "${vcpkg_toolchain}")
        file(MAKE_DIRECTORY "${vcpkg_download_directory}")

        if(NOT EXISTS "${vcpkg_archive}")
            message(STATUS
                "Downloading pinned vcpkg ${AUDIOCOMPD_VCPKG_COMMIT}")
            file(DOWNLOAD
                "https://github.com/microsoft/vcpkg/archive/${AUDIOCOMPD_VCPKG_COMMIT}.tar.gz"
                "${vcpkg_archive}.part"
                STATUS download_status
                TLS_VERIFY ON
                SHOW_PROGRESS)

            list(GET download_status 0 download_result)
            list(GET download_status 1 download_message)
            if(NOT download_result EQUAL 0)
                file(REMOVE "${vcpkg_archive}.part")
                message(FATAL_ERROR "Could not download vcpkg: ${download_message}")
            endif()
            file(RENAME "${vcpkg_archive}.part" "${vcpkg_archive}")
        endif()

        file(REMOVE_RECURSE "${vcpkg_extracted_directory}" "${vcpkg_root}")
        file(ARCHIVE_EXTRACT INPUT "${vcpkg_archive}"
            DESTINATION "${CMAKE_BINARY_DIR}/_deps")

        if(NOT EXISTS "${vcpkg_extracted_directory}/scripts/buildsystems/vcpkg.cmake")
            message(FATAL_ERROR "The downloaded vcpkg archive has an unexpected layout")
        endif()

        file(RENAME "${vcpkg_extracted_directory}" "${vcpkg_root}")
    endif()

    if(NOT EXISTS "${vcpkg_executable}")
        set(vcpkg_metadata "${vcpkg_root}/scripts/vcpkg-tool-metadata.txt")
        _audiocompd_read_vcpkg_metadata(
            "${vcpkg_metadata}" VCPKG_TOOL_RELEASE_TAG vcpkg_tool_release)

        string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" host_processor)
        if(host_processor MATCHES "^(x86_64|amd64)$")
            set(vcpkg_tool_name "vcpkg-glibc")
            set(vcpkg_hash_variable "VCPKG_GLIBC_SHA")
        elseif(host_processor MATCHES "^(aarch64|arm64)$")
            set(vcpkg_tool_name "vcpkg-glibc-arm64")
            set(vcpkg_hash_variable "VCPKG_GLIBC_ARM64_SHA")
        else()
            message(FATAL_ERROR
                "Automatic vcpkg bootstrapping supports Linux x86_64 and arm64; "
                "the detected architecture is ${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()

        _audiocompd_read_vcpkg_metadata(
            "${vcpkg_metadata}" "${vcpkg_hash_variable}" vcpkg_tool_hash)

        message(STATUS "Downloading verified ${vcpkg_tool_name} executable")
        file(DOWNLOAD
            "https://github.com/microsoft/vcpkg-tool/releases/download/${vcpkg_tool_release}/${vcpkg_tool_name}"
            "${vcpkg_executable}"
            EXPECTED_HASH "SHA512=${vcpkg_tool_hash}"
            STATUS tool_download_status
            TLS_VERIFY ON
            SHOW_PROGRESS)

        list(GET tool_download_status 0 tool_download_result)
        list(GET tool_download_status 1 tool_download_message)
        if(NOT tool_download_result EQUAL 0)
            file(REMOVE "${vcpkg_executable}")
            message(FATAL_ERROR
                "Could not download the vcpkg executable: ${tool_download_message}")
        endif()

        file(CHMOD "${vcpkg_executable}"
            PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE)
        file(TOUCH "${vcpkg_root}/vcpkg.disable-metrics")
    endif()

    set(CMAKE_TOOLCHAIN_FILE "${vcpkg_toolchain}" CACHE FILEPATH
        "CMake toolchain provided by audiocompd's pinned vcpkg" FORCE)
    set(CMAKE_TOOLCHAIN_FILE "${vcpkg_toolchain}" PARENT_SCOPE)
    set(VCPKG_MANIFEST_DIR "${source_directory}" CACHE PATH
        "audiocompd vcpkg manifest directory" FORCE)
    set(VCPKG_MANIFEST_DIR "${source_directory}" PARENT_SCOPE)
    set(VCPKG_ROOT "${vcpkg_root}" CACHE PATH
        "Private vcpkg instance bootstrapped by audiocompd" FORCE)
    set(VCPKG_ROOT "${vcpkg_root}" PARENT_SCOPE)
endfunction()

