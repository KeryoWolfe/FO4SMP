cmake_minimum_required(VERSION 3.25)

# FO4SMP: Faster HDT-SMP for Fallout 4 (1.11.240).
# Milestone 1: the plugin loads, logs, checks the addresses it will hook, links FSMP's physics core,
# reads configs.json and defaultBBPs.xml, steps an empty physics world once and reports in the
# console. Milestone 2: one hook steps the physics world every frame (see FO4_PORT.md).

project(
	FO4SMP
	VERSION 0.2.0
	LANGUAGES CXX
)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "FO4SMP is 64-bit only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(FSMP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# CommonLibF4: next-gen / 1.11 runtimes only (the first target is 1.11.240).
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibF4/CommonLibF4" CommonLibF4 EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)  # FSMP's patched Bullet 3.25 (../cmake/ports/bullet3)
find_package(TBB CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)  # configs.json, through FSMP's GlobalConfig (header-only; vcpkg's include dir)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/src/Version.h"
	@ONLY
)

# FSMP's game-free code, shared with the Skyrim build unchanged.
set(FSMP_CORE
	"${FSMP_SRC}/hdtSkinnedMesh/hdtBoneScaleConstraint.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtCollider.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtConeTwistConstraint.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtDispatcher.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtGeneric6DofConstraint.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtSkinnedMeshAlgorithm.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtSkinnedMeshBody.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtSkinnedMeshBone.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtSkinnedMeshShape.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtSkinnedMeshSystem.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtSkinnedMeshWorld.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtStiffSpringConstraint.cpp"
	"${FSMP_SRC}/hdtSkinnedMesh/hdtVertex.cpp"
	"${FSMP_SRC}/XmlReader.cpp"
	"${FSMP_SRC}/GlobalConfig.cpp"
	"${FSMP_SRC}/hdtDefaultBBPList.cpp"
)

add_library(
	"${PROJECT_NAME}"
	SHARED
	src/main.cpp
	src/AddressCheck.cpp
	src/AddressCheck.h
	src/Config.cpp
	src/Config.h
	src/Console.cpp
	src/Console.h
	src/FrameHook.cpp
	src/FrameHook.h
	src/Offsets.h
	src/PCH.h
	src/PhysicsWorld.cpp
	src/PhysicsWorld.h
	${FSMP_CORE}
)

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_BINARY_DIR}/src"
		"${FSMP_SRC}"
		"${FSMP_SRC}/hdtSkinnedMesh"
		${BULLET_INCLUDE_DIRS}
)

# As FSMP's build: Bullet's SIMD API (Bullet itself is built with it, see the port), no min/max macros.
target_compile_definitions("${PROJECT_NAME}" PRIVATE BT_USE_SSE_IN_API NOMINMAX _SILENCE_AMP_DEPRECATION_WARNINGS)

target_link_libraries(
	"${PROJECT_NAME}"
	PRIVATE
		CommonLibF4::CommonLibF4
		spdlog::spdlog
		${BULLET_LIBRARIES}
		TBB::tbb
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE src/PCH.h)

if(MSVC)
	# AVX2, like FSMP's and FO4 Faster HDT-SMP's main builds (the core has an AVX2 skinning path).
	target_compile_options("${PROJECT_NAME}" PRIVATE /arch:AVX2 /W4 /wd4100 /wd4201 /permissive- /Zc:preprocessor /utf-8 /bigobj /MP /EHsc)
	target_link_options("${PROJECT_NAME}" PRIVATE /DEBUG:FULL "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>")
endif()

# Copy the DLL, its PDB and the shipped config files into a mod folder after each build:
#   <dir>/F4SE/Plugins/FO4SMP.dll, <dir>/F4SE/Plugins/FO4SMP/configs.json, defaultBBPs.xml
set(FO4SMP_DEPLOY_DIR "" CACHE PATH "Optional mod folder to copy the build into (e.g. an MO2 mod folder).")
if(FO4SMP_DEPLOY_DIR)
	add_custom_command(
		TARGET "${PROJECT_NAME}" POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${FO4SMP_DEPLOY_DIR}/F4SE/Plugins"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${FO4SMP_DEPLOY_DIR}/F4SE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${FO4SMP_DEPLOY_DIR}/F4SE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/package" "${FO4SMP_DEPLOY_DIR}"
		VERBATIM
	)
endif()
