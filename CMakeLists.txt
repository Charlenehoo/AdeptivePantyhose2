cmake_minimum_required(VERSION 3.22)

project(
	AdeptivePantyhose2
	VERSION 2.0.0
	LANGUAGES CXX
)
set(PROJECT_AUTHOR "CharleneHoo" CACHE STRING "Plugin author")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
	if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif()
endmacro()

set_from_environment(Skyrim64Path)
if(NOT DEFINED Skyrim64Path)
	message(FATAL_ERROR "Skyrim64Path is not set")
endif()

option(COPY_OUTPUT "copy the output of build operations to the game directory" OFF)

option(BUILD_SKYRIMAE "Build for Skyrim AE" ON)
if(BUILD_SKYRIMAE)
    add_compile_definitions(SKYRIM_AE SKYRIM_SUPPORT_AE)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

add_subdirectory(src)
include(cmake/packaging.cmake)
