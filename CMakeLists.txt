cmake_minimum_required(VERSION 3.24)
project(Piggyback VERSION 1.1.1 LANGUAGES CXX)

# Pin upstream source and its submodules for reproducible builds.
if(MSVC)
    # Keep developer paths out of __FILE__, source_location and debug metadata.
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" PIGGYBACK_NATIVE_SOURCE_DIR)
    add_compile_options(/experimental:deterministic
        "/pathmap:${PIGGYBACK_NATIVE_SOURCE_DIR}=Piggyback")
endif()
include(FetchContent)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
FetchContent_Declare(CommonLibSSE
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG bedcb1e05418baba7b316a650b6180c2dd6007a8
    GIT_SUBMODULES extern/openvr
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(CommonLibSSE)
include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)
add_commonlibsse_plugin(${PROJECT_NAME} AUTHOR "Kroosstiich" SOURCES ${headers} ${sources})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# Build locally by default. Deployment is an explicit opt-in.
set(PIGGYBACK_OUTPUT_FOLDER "" CACHE PATH "Optional mod directory for DLL deployment")
if(PIGGYBACK_OUTPUT_FOLDER)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PIGGYBACK_OUTPUT_FOLDER}/SKSE/Plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>"
            "${PIGGYBACK_OUTPUT_FOLDER}/SKSE/Plugins/Piggyback.dll"
        VERBATIM)
endif()
