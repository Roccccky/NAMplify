# Tests ohne Fenster und ohne Host, damit sie bei jedem Build mitlaufen.

file (GLOB NAMPLIFY_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable (namplify_tests ${NAMPLIFY_TEST_SOURCES})
target_link_libraries (namplify_tests PRIVATE namplify_core)
target_include_directories (namplify_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Echte A2-Captures (tests/fixtures/a2). Ein A2-Netz lässt sich nicht von Hand
# erzeugen wie ein Linear-Modell.
target_compile_definitions (namplify_tests PRIVATE
    NAMPLIFY_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    # Die Demo-UI liest ihre Regler aus einer erzeugten Datei
    # (MockDefsTest.cpp).
    NAMPLIFY_UI_DIR="${CMAKE_SOURCE_DIR}/ui")

if (NOT MSVC)
    target_compile_options (namplify_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_test (NAME namplify_tests COMMAND namplify_tests)
