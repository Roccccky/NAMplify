# namplify_core: DSP-Kern ohne GUI-Framework und ohne Abhängigkeit vom
# Betriebssystem. Abhängigkeiten: Standardbibliothek, Eigen (header-only) und
# der NAM-Kern.

file (GLOB_RECURSE NAMPLIFY_CORE_SOURCES CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file (GLOB_RECURSE NAMPLIFY_CORE_HEADERS CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_library (namplify_core STATIC ${NAMPLIFY_CORE_SOURCES} ${NAMPLIFY_CORE_HEADERS})

target_include_directories (namplify_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_features (namplify_core PUBLIC cxx_std_20)

# NAM-Kern als OBJECT-Ziel, siehe cmake/NamCore.cmake.
target_link_libraries (namplify_core PUBLIC namplify_nam)

if (MSVC)
    target_compile_options (namplify_core PRIVATE /W4 /fp:fast)
else()
    target_compile_options (namplify_core PRIVATE
        -Wall -Wextra -Wshadow -Wno-unused-parameter -ffast-math)
endif()

set_target_properties (namplify_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
