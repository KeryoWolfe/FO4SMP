# FO4SMP's vcpkg triplet: static dependencies, dynamic CRT, AVX2 (as FSMP's), built with whatever MSVC
# toolset the build prompt uses.
#
# FSMP's own triplets (../../cmake/triplets) also pin the dependency compiler to MSVC 14.44, so that
# dependencies and the plugin can't be built by different compilers on a machine with several Visual
# Studios. That pin fails outright where 14.44 isn't installed (VS 2026 ships 14.5x). Unpinned, vcpkg uses
# the newest installed toolset; build from that same Visual Studio's x64 Native Tools prompt so the plugin
# matches.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

string(APPEND VCPKG_CXX_FLAGS " /arch:AVX2")
string(APPEND VCPKG_C_FLAGS " /arch:AVX2")
