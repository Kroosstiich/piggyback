set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)

# Use the same compiler toolset as build.ps1 for every vcpkg dependency.
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)

if (${PORT} MATCHES "fully-dynamic-game-engine|skse|qt*")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else ()
    set(VCPKG_LIBRARY_LINKAGE static)
endif ()
