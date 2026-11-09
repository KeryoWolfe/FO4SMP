cmake_minimum_required(VERSION 3.25)

# FO4SMP: Faster HDT-SMP for Fallout 4 (1.11.240).
# Milestone 1: the plugin loads, logs, checks the addresses it will hook, links FSMP's physics core,
# reads configs.json and defaultBBPs.xml, steps an empty physics world once and reports in the
# console. Milestone 2: one hook steps the physics world every frame (see FO4_PORT.md).

project(
	FO4SMP
	VERSION 0.3.0
	LANGUAGES CXX
)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "FO4SMP is 64-bit only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(FSMP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# CommonLibF4: KeryoWolfe/CommonLibF4, branch fo4smp/combined (Dear Modding's tree plus every other fork's fixes, with
# REL::VariantID{ og, ng, ae } and every AE id in Address Library 1.11.240; see its FO4SMP.md). Its own CMakeLists
# builds lib/commonlib-shared too: `git submodule update --init --recursive`.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibF4/lib/commonlib-shared/CMakeLists.txt")
	message(FATAL_ERROR "extern/CommonLibF4's submodules are missing: run git submodule update --init --recursive")
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibF4" CommonLibF4 EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)  # FSMP's patched Bullet 3.25 (../cmake/ports/bullet3)
find_package(TBB CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)  # FSMP's <pattern> expander
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
	"${FSMP_SRC}/Patterns/hdtPatternLibrary.cpp"
	"${FSMP_SRC}/Patterns/hdtXmlPatternExpander.cpp"
	# Milestone 5: the validator's XSD check (FO4SMP_BUILD points it at FO4SMP's schema path).
	"${FSMP_SRC}/Validator/Validators/hdtXSDValidator.cpp"
	"${FSMP_SRC}/Validator/Parser/hdtXSDSchemaParser.cpp"
)

add_library(
	"${PROJECT_NAME}"
	SHARED
	src/main.cpp
	src/AddressCheck.cpp
	src/AddressCheck.h
	src/AttachHook.cpp
	src/AttachHook.h
	src/Config.cpp
	src/Config.h
	src/Console.cpp
	src/Console.h
	src/FrameHook.cpp
	src/FrameHook.h
	src/Papyrus.cpp
	src/Papyrus.h
	src/Settings.cpp
	src/Settings.h
	src/Validation.cpp
	src/Validation.h
	src/NetImmerseUtils.h
	src/Offsets.h
	src/PCH.h
	src/PhysicsWorld.cpp
	src/PhysicsWorld.h
	src/PlayerPhysics.cpp
	src/PlayerPhysics.h
	src/game/Fo4Body.cpp
	src/game/Fo4Body.h
	src/game/Fo4Bone.cpp
	src/game/Fo4Bone.h
	src/game/Fo4ConvertNi.cpp
	src/game/Fo4ConvertNi.h
	src/game/Fo4NiUtils.h
	src/game/Fo4Nodes.cpp
	src/game/Fo4Nodes.h
	src/game/Fo4Resource.cpp
	src/game/Fo4Settings.cpp
	src/game/Fo4Settings.h
	src/game/Fo4Skin.cpp
	src/game/Fo4Skin.h
	src/game/Fo4System.cpp
	src/game/Fo4System.h
	${FSMP_CORE}
)

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/game"
		"${CMAKE_CURRENT_BINARY_DIR}/src"
		"${FSMP_SRC}"
		"${FSMP_SRC}/hdtSkinnedMesh"
		${BULLET_INCLUDE_DIRS}
)

# As FSMP's build: Bullet's SIMD API (Bullet itself is built with it, see the port), no min/max macros.
# HDT_ACCUMULATE_OPPOSING_CONTACTS: FSMP's merge as it was before b102ea8 (May 2026), which A-Body's XML was
# tuned against: contacts of a bone pair that push opposite ways are summed instead of the later ones being dropped.
# Suspected of making the penis between the thighs jump from side to side (run 26); FO4 Faster HDT-SMP predates it.
target_compile_definitions("${PROJECT_NAME}" PRIVATE FO4SMP_BUILD BT_USE_SSE_IN_API NOMINMAX _SILENCE_AMP_DEPRECATION_WARNINGS HDT_ACCUMULATE_OPPOSING_CONTACTS)

target_link_libraries(
	"${PROJECT_NAME}"
	PRIVATE
		CommonLibF4::CommonLibF4
		spdlog::spdlog
		${BULLET_LIBRARIES}
		TBB::tbb
		pugixml::pugixml
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE src/PCH.h)

if(MSVC)
	# AVX2, like FSMP's and FO4 Faster HDT-SMP's main builds (the core has an AVX2 skinning path).
	target_compile_options("${PROJECT_NAME}" PRIVATE /arch:AVX2 /W4 /wd4100 /wd4201 /permissive- /Zc:preprocessor /utf-8 /bigobj /MP /EHsc /Zi
		"$<$<CONFIG:Release>:/Zc:inline;/Ob3>")  # as FSMP's Release build (src/CMakeLists.txt); RelWithDebInfo's /Ob1 hobbles the SIMD collision code
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
