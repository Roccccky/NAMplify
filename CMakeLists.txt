cmake_minimum_required (VERSION 3.22)

project (NAMplify VERSION 2.0.0 LANGUAGES C CXX)

# Version. project() bekommt nur Zahlen, weil Bundle, Installer und Plugin-ID
# das brauchen. Der Zusatz für Vorabversionen erscheint in den Einstellungen und
# im Release-Tag v${NAMPLIFY_VERSION_LABEL} (geprüft von
# tools/verify-release.mjs). Für 2.0.0 bleibt er leer.
set (NAMPLIFY_VERSION_SUFFIX "-beta.2")
set (NAMPLIFY_VERSION_LABEL "${PROJECT_VERSION}${NAMPLIFY_VERSION_SUFFIX}")

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set (CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# macOS: standardmäßig universal (arm64 + x86_64). Ohne FORCE, damit
# -DCMAKE_OSX_ARCHITECTURES=arm64 auf der Befehlszeile für schnelle lokale
# Builds greift.
if (APPLE)
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

option (NAMPLIFY_BUILD_PLUGIN "Standalone, VST3 und AU bauen" ON)
option (NAMPLIFY_BUILD_TESTS  "Prüfungen bauen"               ON)

# TONE3000: der öffentliche Client-Schlüssel wird beim Build eingesetzt. Ohne
# ihn funktioniert alles außer dem TONE3000-Browser, der dann einen Hinweis
# zeigt.
set (NAMPLIFY_T3K_CLIENT_ID "" CACHE STRING "TONE3000 publishable key (t3k_pub_…)")

include (cmake/NamCore.cmake)

add_subdirectory (core)

if (NAMPLIFY_BUILD_PLUGIN)
    add_subdirectory (plugin)
endif()

if (NAMPLIFY_BUILD_TESTS)
    enable_testing()
    add_subdirectory (tests)
endif()
