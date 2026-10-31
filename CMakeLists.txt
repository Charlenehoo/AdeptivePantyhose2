# 使用 3.26：copy_directory_if_different 需要 3.26。
# 不升到 3.31：实测构建步骤会从 393 增至 783，原因待查。
cmake_minimum_required(VERSION 3.26)
project(AdeptivePantyhose VERSION 3.0.5 LANGUAGES CXX)
set(PROJECT_AUTHOR "Charlene Hoo")
set(PROJECT_AUTHOR_EMAIL "CharleneHoo@hotmail.com")

# ─────── 构建开关 ───────
# 所有 option 集中在此，configure 输出会打印每个开关的最终状态。
# 修改后需要重新 configure（cmake --preset ...），只 cmake --build 不生效。

# 详细日志：Release 优化 + DEBUG/TRACE 日志。
# 开启后 LOG_DEBUG / LOG_TRACE 展开为 REX::DEBUG / REX::TRACE，
# 产物名加 _Log 后缀，与普通版部署到不同 MO2 目录。
# 关闭时 LOG_DEBUG / LOG_TRACE 展开为 ((void)0)，参数不求值、
# 字符串不进二进制。
option(ADEPTIVE_PANTYHOSE_DETAILED_LOG "Build with DEBUG/TRACE logging" OFF)

if(ADEPTIVE_PANTYHOSE_DETAILED_LOG)
    message(STATUS "Logging: detailed (DEBUG/TRACE included)")
else()
    message(STATUS "Logging: normal (INFO and above)")
endif()

# 目标平台：SE 1.5.97（默认）或 AE 1.6.1130+。
# 必须在 add_subdirectory(CommonLibSSE) 之前定义 SKYRIM_AE 宏，
# 否则 CommonLibSSE 按 SE 编译，与 AE 产物不匹配。
option(BUILD_SKYRIM_AE "Build for Skyrim AE 1.6.1130+" OFF)

if(BUILD_SKYRIM_AE)
    message(STATUS "Target: Skyrim AE (1.6.1130+)")
    add_compile_definitions(SKYRIM_AE SKYRIM_SUPPORT_AE)
else()
    message(STATUS "Target: Skyrim SE (1.5.97)")
endif()

# ─────── 版本头 ───────
# 从 Plugin.h.in 生成带版本号、项目名的 Plugin.h。
set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/Plugin.h")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in"
    "${VERSION_HEADER}"
    @ONLY
)

# ─────── 本项目 target ───────
add_library(
    "${PROJECT_NAME}" SHARED
    src/main.cpp
    src/Config/SyntaxParser.cpp
    src/Event/EventProcessor.cpp
    src/Integration/SKEE/AttachListener.cpp
    src/Integration/SKEE/SKEEIntegration.cpp
    src/MorphRuleEngine/PredicateCompiler.cpp
    src/MorphRuleEngine/MorphRuleEngine.cpp
    src/MorphRuleEngine/Leaves/KeywordPredicate.cpp
    src/MorphRuleEngine/Leaves/FormIDRangePredicate.cpp
)
target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

if(ADEPTIVE_PANTYHOSE_DETAILED_LOG)
    target_compile_definitions("${PROJECT_NAME}" PRIVATE
        ADEPTIVE_PANTYHOSE_DETAILED_LOG)
endif()

target_include_directories(
    "${PROJECT_NAME}" PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/src"
)

if(MSVC)
    target_compile_options("${PROJECT_NAME}" PRIVATE
        /utf-8
        /Zc:__cplusplus
        /permissive-
        /EHsc
    )
endif()

# ─────── 产物名 ───────
# SE / AE 不能互相覆盖。TARGET_SUFFIX 同时用于 DLL 名和 MO2 目录名。
if(BUILD_SKYRIM_AE)
    set(TARGET_SUFFIX "AE")
else()
    set(TARGET_SUFFIX "SE")
endif()
if(ADEPTIVE_PANTYHOSE_DETAILED_LOG)
    string(APPEND TARGET_SUFFIX "_Log")
endif()
set_target_properties("${PROJECT_NAME}" PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}_${TARGET_SUFFIX}")
message(STATUS "Output: ${PROJECT_NAME}_${TARGET_SUFFIX}.dll")

# ─────── 第三方依赖 ───────
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

# ─────── 链接与最终配置 ───────
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
if(DEFINED ENV{MO2MODS})
    set(MO2_MOD_NAME "${PROJECT_NAME}_${TARGET_SUFFIX}")

    set(MO2_MOD_DIR "$ENV{MO2MODS}/${MO2_MOD_NAME}")
    cmake_path(NORMAL_PATH MO2_MOD_DIR)
    cmake_path(APPEND MO2_MOD_DIR "SKSE" "Plugins"
        OUTPUT_VARIABLE MO2_PLUGIN_DIR)

    # ---- DLL ----
    add_custom_command(
        TARGET "${PROJECT_NAME}" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MO2_PLUGIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${MO2_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Copying $<TARGET_FILE_NAME:${PROJECT_NAME}> to MO2: ${MO2_PLUGIN_DIR}"
    )

    # ---- PDB（Debug）----
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
    message(STATUS "MO2MODS environment variable not found. Skipping auto-copy.")
endif()

# ─────── 发布打包 ───────
# 手动触发：cmake --build --preset msvc-ae-release --target package
# 生成 dist/AdeptivePantyhose_<后缀>_v<版本>.7z，内含 SKSE/... 结构，
# 不含 PDB（PDB 只用于本地测试部署）。
find_program(SEVEN_ZIP_EXE NAMES 7z 7za 7zz)
if(NOT SEVEN_ZIP_EXE)
    message(STATUS "7z not found, package target disabled")
else()
    set(PACKAGE_STAGE "${CMAKE_CURRENT_BINARY_DIR}/package-stage")
    set(PACKAGE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/dist")
    set(PACKAGE_ARCHIVE "${PACKAGE_OUTPUT_DIR}/${PROJECT_NAME}_${TARGET_SUFFIX}_v${PROJECT_VERSION}.7z")

    add_custom_target(package
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${PACKAGE_ARCHIVE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_STAGE}/SKSE/Plugins"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${PACKAGE_STAGE}/SKSE/Plugins/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${PACKAGE_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${PACKAGE_STAGE}"
        ${SEVEN_ZIP_EXE} a -t7z -mx=7 "${PACKAGE_ARCHIVE}" .
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_STAGE}"
        COMMENT "Packing ${PACKAGE_ARCHIVE}"
        DEPENDS "${PROJECT_NAME}"
        VERBATIM
    )
endif()

# ─────── 日志硬链接 ───────
# Windows only。硬链接失败时回退到复制（跨卷情况）。
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
        # 默认硬链接；跨卷时回退到复制。无需管理员权限。
        file(CREATE_LINK "${_log}" "${_link}" COPY_ON_ERROR RESULT _err)
        if(_err)
            message(WARNING "Failed to link SKSE log: ${_err}")
        else()
            message(STATUS "Linked SKSE log: ${_link} -> ${_log}")
        endif()
    endif()
endif()
