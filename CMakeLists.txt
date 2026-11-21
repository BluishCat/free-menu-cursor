cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED ENV{COMMONLIB_SSE_FOLDER})
    message(FATAL_ERROR "Missing COMMONLIB_SSE_FOLDER environment variable")
endif()

project(FreeMenuCursor VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)  # VR is untested; SE/AE only
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("$ENV{COMMONLIB_SSE_FOLDER}" ${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build)
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include(${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "Free Menu Cursor"
    AUTHOR "BluishCat"
    SOURCES ${sources}
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Free Menu Cursor")

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    # /d1trimfile strips the local source root from __FILE__/source_location strings;
    # /PDBALTPATH stores only the PDB file name in the DLL (no local build path).
    # Trim the shared workspace root (parent of this project and of the CommonLib checkout).
    get_filename_component(FMC_WORKSPACE "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(TO_NATIVE_PATH "${FMC_WORKSPACE}/" FMC_TRIM_ROOT)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 "$<$<CONFIG:Release>:/Zi>")
    # Own call: the trailing backslash would otherwise escape the list separator.
    target_compile_options(${PROJECT_NAME} PRIVATE "/d1trimfile:${FMC_TRIM_ROOT}")
    target_compile_options(CommonLibSSE PRIVATE "/d1trimfile:${FMC_TRIM_ROOT}")
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>" "/PDBALTPATH:%_PDB%")
endif()

# Helper started only when a pointer override is enabled: restores the Windows pointer
# if the game exits without running the plugin's cleanup.
add_executable(FreeMenuCursorRestore WIN32 helper/RestoreHelper.cpp)
set_target_properties(FreeMenuCursorRestore PROPERTIES OUTPUT_NAME "Free Menu Cursor Restore")
target_compile_definitions(FreeMenuCursorRestore PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(FreeMenuCursorRestore PRIVATE shell32 user32)
if(MSVC)
    target_compile_options(FreeMenuCursorRestore PRIVATE /utf-8 /W4)
    target_link_options(FreeMenuCursorRestore PRIVATE "/PDBALTPATH:%_PDB%")
endif()
