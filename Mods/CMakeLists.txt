cmake_minimum_required(VERSION 3.16)
project(nfsmw_mods LANGUAGES C CXX)

# Build our mods against the SDK clone without going through its root
# CMakeLists (we only want the plugin helper, not the examples/install rules).
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/../Tools/nfsmw-2005-sdk")
set(NFSMW_SDK_INCLUDE_DIR  "${SDK}/include")
set(NFSMW_SDK_ENTRY_SOURCE "${SDK}/src/entry.c")
set(NFSMW_SDK_MINHOOK_DIR  "${SDK}/extern/minhook")
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Same runtime linkage as the SDK's own toolchain file: the game folder has no MinGW DLLs.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -Wl,--kill-at")
include("${SDK}/cmake/nfsmw_sdkHelpers.cmake")

nfsmw_add_plugin(nfsmw_hello SOURCES hello/main.cpp)
nfsmw_add_plugin(nfsmw_cam   SOURCES cam/main.cpp)
