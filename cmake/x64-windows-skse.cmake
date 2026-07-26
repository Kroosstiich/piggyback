set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)

# Epingle le toolset MSVC v143 (14.44) pour TOUS les builds de ports vcpkg. Sinon vcpkg refait sa
# propre detection et prend le v145 (14.51, VS 2026) par defaut, qui casse d'anciennes deps comme
# fmt 9.1.0 (symbole stdext::checked_array_iterator supprime en v145). Voir docs/02-plugin-rig-moteur.md.
set(VCPKG_PLATFORM_TOOLSET v143)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)

if (${PORT} MATCHES "fully-dynamic-game-engine|skse|qt*")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
else ()
    set(VCPKG_LIBRARY_LINKAGE static)
endif ()
