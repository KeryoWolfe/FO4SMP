cmake_minimum_required(VERSION 3.25)

# FO4SMP: Faster HDT-SMP for Fallout 4 (1.11.240).
# Milestone 1: the plugin loads, logs, checks the addresses it will hook and
# finds its config folder. The physics core is not linked yet (see FO4_PORT.md).

project(
	FO4SMP
	VERSION 0.1.0
	LANGUAGES CXX
)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "FO4SMP is 64-bit only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibF4: next-gen / 1.11 runtimes only (the first target is 1.11.240).
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibF4/CommonLibF4" CommonLibF4 EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/src/Version.h"
	@ONLY
)

add_library(
	"${PROJECT_NAME}"
	SHARED
	src/main.cpp
	src/AddressCheck.cpp
	src/AddressCheck.h
	src/Offsets.h
	src/PCH.h
)

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_BINARY_DIR}/src"
)

target_link_libraries(
	"${PROJECT_NAME}"
	PRIVATE
		CommonLibF4::CommonLibF4
		spdlog::spdlog
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE src/PCH.h)

if(MSVC)
	target_compile_options("${PROJECT_NAME}" PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /MP)
	target_link_options("${PROJECT_NAME}" PRIVATE /DEBUG:FULL "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>")
endif()

# Copy the DLL (and PDB) into a mod folder layout: <out>/F4SE/Plugins/FO4SMP.dll
set(FO4SMP_DEPLOY_DIR "" CACHE PATH "Optional mod folder to copy F4SE/Plugins/FO4SMP.dll into after each build (e.g. an MO2 mod folder).")
if(FO4SMP_DEPLOY_DIR)
	add_custom_command(
		TARGET "${PROJECT_NAME}" POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${FO4SMP_DEPLOY_DIR}/F4SE/Plugins"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${FO4SMP_DEPLOY_DIR}/F4SE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${FO4SMP_DEPLOY_DIR}/F4SE/Plugins/"
		VERBATIM
	)
endif()
