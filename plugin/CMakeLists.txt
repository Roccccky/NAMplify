# NAMplify: Standalone, VST3, AU

set (JUCE_DIR "${CMAKE_SOURCE_DIR}/third_party/JUCE")
if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message (FATAL_ERROR "JUCE fehlt unter ${JUCE_DIR}")
endif()

set (JUCE_BUILD_EXTRAS   OFF CACHE BOOL "" FORCE)
set (JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory ("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

# Die UI wird ins Binary eingebettet, damit sie nicht von Dateien neben dem
# Programm abhängt. Die Dateien liegen flach in ui/, damit die Pfade im Browser
# (Demo) und im eingebetteten Fenster gleich sind.
file (GLOB NAMPLIFY_UI_FILES CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/ui/*.html"
      "${CMAKE_SOURCE_DIR}/ui/*.css"
      "${CMAKE_SOURCE_DIR}/ui/*.js")

file (GLOB NAMPLIFY_BUNDLED_IRS CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/assets/bundled-irs/*.wav"
      "${CMAKE_SOURCE_DIR}/assets/bundled-irs/*.t3k.json"
      "${CMAKE_SOURCE_DIR}/assets/bundled-irs/*.jpg")

juce_add_binary_data (namplify_resources
    HEADER_NAME NamplifyResources.h
    NAMESPACE   NamplifyResources
    SOURCES     ${NAMPLIFY_UI_FILES} ${NAMPLIFY_BUNDLED_IRS})

set_target_properties (namplify_resources PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Redirect-URL der TONE3000-Anmeldung, so bei TONE3000 eingetragen. Wird als
# NAMPLIFY_T3K_REDIRECT an den Code und an die Info.plist des Standalone
# übergeben.
set (NAMPLIFY_URL_SCHEME   "namplify")
set (NAMPLIFY_T3K_REDIRECT "${NAMPLIFY_URL_SCHEME}://t3k-callback")

juce_add_plugin (NAMplify
    PRODUCT_NAME                "NAMplify"
    COMPANY_NAME                "NAMplify.app"
    COMPANY_WEBSITE             "https://namplify.app"
    BUNDLE_ID                   "de.martinmaiwald.namplify.plugin"
    PLUGIN_MANUFACTURER_CODE    Nmpa
    PLUGIN_CODE                 Nmpl
    FORMATS                     Standalone VST3 AU
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    ICON_BIG                    "${CMAKE_SOURCE_DIR}/assets/icon/mac_1024.png"
    ICON_SMALL                  "${CMAKE_SOURCE_DIR}/assets/icon/mac_128.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "NAMplify needs the input to hear your guitar."
    NEEDS_WEB_BROWSER           TRUE
    # Nur unter Linux relevant: dort hat JUCE ohne curl kein HTTPS. macOS und
    # Windows nutzen die Netzwerkbibliothek des Systems.
    NEEDS_CURL                  TRUE)

# .mm für die Titelleiste (Erscheinungsbild unter macOS, Fensterattribut unter
# Windows).
file (GLOB_RECURSE NAMPLIFY_PLUGIN_SOURCES CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

if (APPLE)
    file (GLOB_RECURSE NAMPLIFY_PLUGIN_OBJC CONFIGURE_DEPENDS
          "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
    list (APPEND NAMPLIFY_PLUGIN_SOURCES ${NAMPLIFY_PLUGIN_OBJC})
else()
    # Unter Windows und Linux als normales C++ übersetzt. Unter Linux liefert
    # die Funktion nur 0.
    set_source_files_properties ("${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DarkTitleBar.mm"
                                 PROPERTIES LANGUAGE CXX)
    list (APPEND NAMPLIFY_PLUGIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/DarkTitleBar.mm")
endif()

target_sources (NAMplify PRIVATE ${NAMPLIFY_PLUGIN_SOURCES})

target_include_directories (NAMplify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ASIO unter Windows. WASAPI und DirectSound liegen bei 20 bis 50 ms,
# ASIO-Treiber von Audio-Interfaces bei etwa 5 ms.
#
# Das SDK gehört Steinberg und liegt nicht im Repo. Pfad über
# -DNAMPLIFY_ASIO_SDK=<Pfad>. Ohne SDK wird ohne ASIO gebaut, mit Hinweis.
set (NAMPLIFY_ASIO_SDK "" CACHE PATH "Steinberg ASIO SDK (Ordner mit common/iasiodrv.h)")

# WebView2 für den Windows-Build. JUCE sucht WebView2.h im Include-Pfad des
# Ziels, nicht über CMAKE_PREFIX_PATH. Ohne diese Zeilen scheitert
# juce_gui_extra.cpp mit "Cannot open include file".
# PUBLIC, weil juce_add_plugin mehrere Ziele anlegt (Standalone, VST3).
set (NAMPLIFY_WEBVIEW2_DIR "" CACHE PATH "NuGet-Paket Microsoft.Web.WebView2")
if (WIN32)
    if (NAMPLIFY_WEBVIEW2_DIR AND EXISTS "${NAMPLIFY_WEBVIEW2_DIR}/build/native/include/WebView2.h")
        message (STATUS "NAMplify: WebView2 aus ${NAMPLIFY_WEBVIEW2_DIR}")
        target_include_directories (NAMplify PUBLIC
            "${NAMPLIFY_WEBVIEW2_DIR}/build/native/include")
        target_link_libraries (NAMplify PUBLIC
            "${NAMPLIFY_WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib")
    else()
        message (FATAL_ERROR "NAMplify: WebView2 fehlt. Ohne sie hat das Programm unter "
                             "Windows ein leeres Fenster. -DNAMPLIFY_WEBVIEW2_DIR=<NuGet-Ordner>")
    endif()

    if (NAMPLIFY_ASIO_SDK AND EXISTS "${NAMPLIFY_ASIO_SDK}/common/iasiodrv.h")
        message (STATUS "NAMplify: ASIO an (${NAMPLIFY_ASIO_SDK})")
        target_compile_definitions (NAMplify PUBLIC JUCE_ASIO=1)
        target_include_directories (NAMplify PRIVATE
            "${NAMPLIFY_ASIO_SDK}/common"
            "${NAMPLIFY_ASIO_SDK}/host"
            "${NAMPLIFY_ASIO_SDK}/host/pc")
    else()
        message (WARNING "NAMplify: ohne ASIO, kein SDK unter NAMPLIFY_ASIO_SDK. "
                         "Unter Windows bleibt nur WASAPI mit deutlich höherer Latenz.")
    endif()
endif()

target_compile_definitions (NAMplify PUBLIC
    JUCE_WEB_BROWSER=1              # UI als eingebettete Webseite
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    # WinRT-MIDI unter Windows. Die alte Win32-Schnittstelle kennt nur USB, kein
    # Bluetooth-MIDI, und keine Ports, die ein anderes Programm schon offen hat.
    # Wo WinRT fehlt, fällt JUCE automatisch zurück.
    JUCE_USE_WINRT_MIDI=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    NAMPLIFY_VERSION_LABEL="${NAMPLIFY_VERSION_LABEL}"
    NAMPLIFY_T3K_CLIENT_ID="${NAMPLIFY_T3K_CLIENT_ID}"
    NAMPLIFY_T3K_REDIRECT="${NAMPLIFY_T3K_REDIRECT}")

# curl nur unter Linux (siehe NEEDS_CURL). Dort setzt JUCE den Schalter selbst
# pro Ziel.
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions (NAMplify PUBLIC JUCE_USE_CURL=0)
endif()

target_link_libraries (NAMplify
    PRIVATE
        namplify_core
        namplify_resources
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if (NOT MSVC)
    target_compile_options (NAMplify PRIVATE -Wno-unused-parameter)
endif()

# Die NAM-Objekte direkt an die Endziele linken.
#
# JUCE packt den gemeinsamen Code in libNAMplify_SharedCode.a. Beim finalen
# Linken (Standalone, VST3, AU) werden aus dieser Bibliothek nur referenzierte
# Objekte übernommen, die statischen Registrierungen der NAM-Architekturen
# fehlen dann, und get_dsp wirft "No config parser registered for architecture".
#
# Deshalb bekommen die Endziele die Objekte direkt, ohne -force_load oder
# /WHOLEARCHIVE. tools/build-and-test.sh prüft im fertigen Programm, dass alle
# sechs Architekturen enthalten sind.
get_target_property (NAMPLIFY_FORMAT_TARGETS NAMplify JUCE_ACTIVE_PLUGIN_TARGETS)

foreach (format_target IN LISTS NAMPLIFY_FORMAT_TARGETS)
    if (TARGET ${format_target})
        target_sources (${format_target} PRIVATE $<TARGET_OBJECTS:namplify_nam_obj>)
    endif()
endforeach()

# Nur das Standalone registriert das URL-Schema. Ohne den Eintrag stellt macOS
# namplify://t3k-callback nicht zu und die Anmeldung kommt nicht zurück.
# Plugin-Bundles dürfen das Schema nicht registrieren.
if (APPLE AND TARGET NAMplify_Standalone)
    add_custom_command (TARGET NAMplify_Standalone POST_BUILD
        COMMAND "${CMAKE_SOURCE_DIR}/tools/register-url-scheme.sh"
                "$<TARGET_BUNDLE_CONTENT_DIR:NAMplify_Standalone>/Info.plist"
                "${NAMPLIFY_URL_SCHEME}"
                "de.martinmaiwald.namplify"
        VERBATIM
        COMMENT "Adressschema ${NAMPLIFY_URL_SCHEME}:// in die Info.plist eintragen")
endif()
