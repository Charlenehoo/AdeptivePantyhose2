# 使用 3.26：copy_directory_if_different 需要 3.26。
# 不升到 3.31：实测构建步骤会从 393 增至 783，原因待查。
cmake_minimum_required(VERSION 3.26)
project(AdeptivePantyhose VERSION 3.0.1 LANGUAGES CXX)
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

# ─────── 目标平台：SE 1.5.97 / AE 1.6.1130+ ───────
# 注意：这两个宏必须在 add_subdirectory(CommonLibSSE) 之前定义，
# 否则 CommonLibSSE 会按默认（SE）编译，与 AE 产物不匹配。
option(BUILD_SKYRIM_AE "Build for Skyrim AE" OFF)

if(BUILD_SKYRIM_AE)
    add_compile_definitions(SKYRIM_AE SKYRIM_SUPPORT_AE)
    message(STATUS "Building for Skyrim AE (1.6.1130+)")
else()
    message(STATUS "Building for Skyrim SE 1.5.97")
endif()

add_library(
    "${PROJECT_NAME}" SHARED
    src/main.cpp
    src/Config/SyntaxParser.cpp
    src/Event/EventProcessor.cpp
    src/Integration/SKEE/SKEEIntegration.cpp
    src/MorphRuleEngine/PredicateCompiler.cpp
    src/MorphRuleEngine/MorphRuleEngine.cpp
    src/MorphRuleEngine/Leaves/KeywordPredicate.cpp
    src/MorphRuleEngine/Leaves/FormIDRangePredicate.cpp
)
target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

# ─────── 产物名区分：SE / AE 不能互相覆盖 ───────
if(BUILD_SKYRIM_AE)
    set_target_properties("${PROJECT_NAME}" PROPERTIES OUTPUT_NAME "${PROJECT_NAME}_AE")
else()
    set_target_properties("${PROJECT_NAME}" PROPERTIES OUTPUT_NAME "${PROJECT_NAME}_SE")
endif()

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

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE" CommonLibSSE EXCLUDE_FROM_ALL SYSTEM)
if(TARGET CommonLibSSE)
    target_compile_options(CommonLibSSE PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-w>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-w>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w>
    )
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendor/SKEE" SKEE)

find_package(absl CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(
    "${PROJECT_NAME}" PRIVATE
    SKEE::SKEE
    CommonLibSSE::CommonLibSSE
    absl::strings
    nlohmann_json::nlohmann_json
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE src/pch.h)

# ─────── MO2 自动部署 ───────
# SE 与 AE 部署到不同的 mod 目录，避免互相覆盖。
# 用户按游戏版本在 MO2 里启用对应的那个 mod。
if(DEFINED ENV{MO2})
    if(BUILD_SKYRIM_AE)
        set(MO2_MOD_NAME "${PROJECT_NAME}_AE")
    else()
        set(MO2_MOD_NAME "${PROJECT_NAME}_SE")
    endif()

    set(MO2_MOD_DIR "$ENV{MO2}/mods/${MO2_MOD_NAME}")
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
