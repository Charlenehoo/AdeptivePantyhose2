# 使用 3.26：copy_directory_if_different 需要 3.26。
# 不升到 3.31：实测构建步骤会从 393 增至 783，原因待查。
cmake_minimum_required(VERSION 3.26)
project(AdeptivePantyhose VERSION 2.0.0 LANGUAGES CXX)
set(PROJECT_AUTHOR "Charlene Hoo")
set(PROJECT_AUTHOR_EMAIL "CharleneHoo@hotmail.com")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/Plugin.h")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in"
    "${VERSION_HEADER}"
    @ONLY
)

add_library(
    "${PROJECT_NAME}" SHARED
    src/main.cpp
    src/Event/EventProcessor.cpp
    src/Event/InputProcessor.cpp
    src/Integration/SKEE/SKEEIntegration.cpp
    src/Integration/Precision/PrecisionIntegration.cpp
    src/Integration/NGD/NGDecapitationsIntegration.cpp
    src/Debug/DecapitateTest.cpp
    src/Debug/CloneCorpse.cpp
)
target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options("${PROJECT_NAME}" PRIVATE
        /utf-8
        /Zc:__cplusplus
        /permissive-
    )
endif()

target_include_directories(
    "${PROJECT_NAME}" PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/src"
)

set(SKYRIM_SUPPORT_AE ON)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE" CommonLibSSE EXCLUDE_FROM_ALL SYSTEM)
if(TARGET CommonLibSSE)
    target_compile_options(CommonLibSSE PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-w>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-w>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w>
    )
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendor/PrecisionAPI" PrecisionAPI)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendor/SKEE" SKEE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendor/NGD" NGD)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

target_link_libraries(
    "${PROJECT_NAME}" PRIVATE
    PrecisionAPI::PrecisionAPI
    SKEE::SKEE
    NGD::NGD
    CommonLibSSE::CommonLibSSE
    nlohmann_json::nlohmann_json
    magic_enum::magic_enum
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE src/pch.h)

if(DEFINED ENV{MO2})
    set(MO2_MOD_DIR "$ENV{MO2}/mods/${PROJECT_NAME}")
    set(MO2_PLUGIN_DIR "${MO2_MOD_DIR}/SKSE/Plugins")

    # ---- DLL ----
    add_custom_command(
        TARGET "${PROJECT_NAME}" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MO2_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${MO2_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Copying $<TARGET_FILE_NAME:${PROJECT_NAME}> to MO2: ${MO2_PLUGIN_DIR}"
    )

    # ---- PDB (Debug) ----
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}" POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
            "${MO2_PLUGIN_DIR}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            COMMENT "Copying $<TARGET_PDB_FILE_NAME:${PROJECT_NAME}> to MO2: ${MO2_PLUGIN_DIR}"
        )
    endif()

    # ---- assets ----
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        add_custom_target("${PROJECT_NAME}_assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${MO2_MOD_DIR}"
            COMMENT "Syncing assets -> ${MO2_MOD_DIR}"
            VERBATIM
        )
        add_dependencies("${PROJECT_NAME}" "${PROJECT_NAME}_assets")
    else()
        message(STATUS "assets/ not found, skipping asset sync")
    endif()

    message(STATUS "MO2 detected. Mod dir: ${MO2_MOD_DIR}")
else()
    message(STATUS "MO2 environment variable not found. Skipping auto-copy.")
endif()

# ─────── Link ${PROJECT_NAME}.log into project root (Windows only) ───────
if(WIN32)
    execute_process(
        COMMAND powershell -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')"
        OUTPUT_VARIABLE _docs OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )

    set(_log "${_docs}/My Games/Skyrim Special Edition/SKSE/${PROJECT_NAME}.log")
    set(_link "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}.log")

    if(NOT EXISTS "${_log}")
        message(STATUS "SKSE log not found at: ${_log}. Run the game once to generate it.")
    elseif(EXISTS "${_link}")
        message(STATUS "Log link already exists. Skipping: ${_link}")
    else()
        # Hard link by default; falls back to a copy if the source and
        # destination live on different volumes. No admin rights needed.
        file(CREATE_LINK "${_log}" "${_link}" COPY_ON_ERROR RESULT _err)
        if(_err)
            message(WARNING "Failed to link SKSE log: ${_err}")
        else()
            message(STATUS "Linked SKSE log: ${_link} -> ${_log}")
        endif()
    endif()
endif()
