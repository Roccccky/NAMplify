# NAM-Kern, unverändert eingebunden.
#
# Als OBJECT-Ziel statt als statische Bibliothek: die Architekturen (WaveNet,
# LSTM, ConvNet, Linear, Sequential, SlimmableContainer) registrieren sich über
# statische Objekte, auf die nichts direkt verweist. In einer statischen
# Bibliothek würde der Linker sie verwerfen, und get_dsp meldet zur Laufzeit "No
# config parser registered for architecture". NamRegistryTest prüft die
# Registry.

set (NAM_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/NeuralAmpModelerCore")
set (EIGEN_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/eigen")

if (NOT EXISTS "${NAM_DIR}/NAM/get_dsp.h")
    message (FATAL_ERROR "NeuralAmpModelerCore fehlt unter ${NAM_DIR}")
endif()
if (NOT EXISTS "${EIGEN_DIR}/Eigen/Dense")
    message (FATAL_ERROR "Eigen fehlt unter ${EIGEN_DIR}")
endif()

set (NAM_SOURCES
    "${NAM_DIR}/NAM/activations.cpp"
    "${NAM_DIR}/NAM/container.cpp"
    "${NAM_DIR}/NAM/conv1d.cpp"
    "${NAM_DIR}/NAM/convnet.cpp"
    "${NAM_DIR}/NAM/dsp.cpp"
    "${NAM_DIR}/NAM/get_dsp.cpp"
    "${NAM_DIR}/NAM/linear.cpp"
    "${NAM_DIR}/NAM/lstm.cpp"
    "${NAM_DIR}/NAM/nam_file.cpp"
    "${NAM_DIR}/NAM/ring_buffer.cpp"
    "${NAM_DIR}/NAM/sequential.cpp"
    "${NAM_DIR}/NAM/util.cpp"
    "${NAM_DIR}/NAM/wavenet/a2_fast.cpp"
    "${NAM_DIR}/NAM/wavenet/model.cpp"
    "${NAM_DIR}/NAM/wavenet/slimmable.cpp")

add_library (namplify_nam_obj OBJECT ${NAM_SOURCES})

target_include_directories (namplify_nam_obj SYSTEM PUBLIC
    "${NAM_DIR}"
    "${NAM_DIR}/Dependencies/nlohmann"
    "${EIGEN_DIR}")

target_compile_features (namplify_nam_obj PUBLIC cxx_std_20)

target_compile_definitions (namplify_nam_obj PUBLIC
    # Alles in float. double bringt bei Captures keinen hörbaren Unterschied.
    NAM_SAMPLE_FLOAT
    # Optimierter A2-Pfad (für FULL und LITE).
    NAM_ENABLE_A2_FAST)

if (MSVC)
    target_compile_definitions (namplify_nam_obj PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options (namplify_nam_obj PRIVATE /W0 /bigobj)
else()
    target_compile_options (namplify_nam_obj PRIVATE -w)
endif()

set_target_properties (namplify_nam_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Interface-Ziel, damit abhängige Ziele Objekte und Include-Pfade bekommen.
add_library (namplify_nam INTERFACE)
target_sources (namplify_nam INTERFACE $<TARGET_OBJECTS:namplify_nam_obj>)
target_link_libraries (namplify_nam INTERFACE namplify_nam_obj)
