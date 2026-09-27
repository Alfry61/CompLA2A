# =============================================================================
#  ALAP Opto Leveler - emulazione di compressore ottico (JUCE 8)
#  Struttura attesa della cartella:
#     CMakeLists.txt
#     build.bat
#     Source/PluginProcessor.h/.cpp
#     Source/PluginEditor.h/.cpp
#     JUCE/            (facoltativa: copia locale di JUCE per lavorare offline)
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(ALAPOptoLeveler VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime MSVC statico: il plugin non dipende dai redistribuibili di Visual C++
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Raggruppa i target in cartelle dentro Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# -----------------------------------------------------------------------------
#  Reperimento di JUCE
#  1) se esiste la copia condivisa in F:/_AI_PROJECTS/SDKs/JUCE viene usata quella
#  2) altrimenti viene cercata una cartella ./JUCE accanto al progetto
#  3) altrimenti JUCE viene scaricato da GitHub alla versione indicata
#  (usare le barre "/" anche su Windows)
# -----------------------------------------------------------------------------
set(JUCE_LOCAL_DIR "F:/_AI_PROJECTS/SDKs/JUCE" CACHE PATH "Percorso di una copia locale di JUCE")

if(NOT EXISTS "${JUCE_LOCAL_DIR}/CMakeLists.txt" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(JUCE_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
set(JUCE_GIT_TAG   "8.0.14" CACHE STRING "Versione di JUCE da scaricare")

if(EXISTS "${JUCE_LOCAL_DIR}/CMakeLists.txt")
    message(STATUS "Uso JUCE locale: ${JUCE_LOCAL_DIR}")
    add_subdirectory("${JUCE_LOCAL_DIR}" JUCE)
else()
    message(STATUS "Scarico JUCE ${JUCE_GIT_TAG} da GitHub...")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${JUCE_GIT_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# -----------------------------------------------------------------------------
#  Definizione del plugin
# -----------------------------------------------------------------------------
juce_add_plugin(ALAPOptoLeveler
    COMPANY_NAME                "Al Learned Audio Productions"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.alap.optoleveler"
    PRODUCT_NAME                "ALAP Opto Leveler"
    PLUGIN_MANUFACTURER_CODE    Alap          # 4 caratteri, almeno una maiuscola
    PLUGIN_CODE                 Opt2          # 4 caratteri, univoco per plugin
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    # La copia automatica in C:\Program Files\Common Files\VST3 richiede
    # privilegi di amministratore: la gestisce build.bat con l'opzione "install"
    COPY_PLUGIN_AFTER_BUILD     FALSE)

# Genera JuceHeader.h, cosi' i sorgenti con #include <JuceHeader.h>
# compilano senza modifiche
juce_generate_juce_header(ALAPOptoLeveler)

# -----------------------------------------------------------------------------
#  Risorse grafiche incorporate nel plugin (logo)
#  Genera BinaryData.h con BinaryData::ALAP_Logo_png e ALAP_Logo_pngSize
# -----------------------------------------------------------------------------
juce_add_binary_data(ALAPOptoAssets
    SOURCES
        Resources/ALAP_Logo.png)

set_target_properties(ALAPOptoAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(ALAPOptoLeveler
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PresetManager.cpp)

target_compile_definitions(ALAPOptoLeveler
    PUBLIC
        JUCE_WEB_BROWSER=0                # non serve il browser integrato
        JUCE_USE_CURL=0                   # nessuna rete
        JUCE_VST3_CAN_REPLACE_VST2=0      # non abbiamo una versione VST2
        JUCE_DISPLAY_SPLASH_SCREEN=0)

# -----------------------------------------------------------------------------
#  Driver ASIO (solo Windows)
#  Riguarda l'applicazione autonoma: dentro Cubase/Ableton il VST3 usa
#  il driver audio scelto dalla DAW.
#  JUCE richiede che la cartella "common" dell'SDK (con iasiodrv.h)
#  sia nel percorso di inclusione.
# -----------------------------------------------------------------------------
set(ASIO_SDK_DIR "F:/_AI_PROJECTS/SDKs/ASIO/asiosdk_2.3.3" CACHE PATH "Percorso dell'SDK ASIO di Steinberg")

if(WIN32)
    # Lo zip di Steinberg a volte contiene una sottocartella con la data:
    # se "common" non e' al primo livello la cerco un livello piu' in basso
    set(ASIO_COMMON_DIR "")
    if(EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        set(ASIO_COMMON_DIR "${ASIO_SDK_DIR}/common")
    else()
        file(GLOB ASIO_NESTED_HEADER "${ASIO_SDK_DIR}/*/common/iasiodrv.h")
        if(ASIO_NESTED_HEADER)
            list(GET ASIO_NESTED_HEADER 0 ASIO_FIRST_HEADER)
            get_filename_component(ASIO_COMMON_DIR "${ASIO_FIRST_HEADER}" DIRECTORY)
        endif()
    endif()

    if(ASIO_COMMON_DIR)
        message(STATUS "ASIO abilitato: ${ASIO_COMMON_DIR}")
        target_include_directories(ALAPOptoLeveler PUBLIC "${ASIO_COMMON_DIR}")
        target_compile_definitions(ALAPOptoLeveler PUBLIC JUCE_ASIO=1)
    else()
        message(WARNING "SDK ASIO non trovato in ${ASIO_SDK_DIR}: "
                        "l'applicazione autonoma usera' solo WASAPI/DirectSound. "
                        "Verificare che esista la cartella common con iasiodrv.h")
    endif()
endif()

target_link_libraries(ALAPOptoLeveler
    PRIVATE
        ALAPOptoAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
#  Programma di prova (applicazione da riga di comando)
#  Compila di nuovo i sorgenti del processore insieme a Tests/TestMain.cpp,
#  senza la parte plugin: misura curva, tempi, armoniche e aliasing.
#  Disattivabile con -DALAP_BUILD_TESTS=OFF
# -----------------------------------------------------------------------------
option(ALAP_BUILD_TESTS "Compila il programma di prova" ON)

if(ALAP_BUILD_TESTS)
    juce_add_console_app(ALAPOptoTests
        PRODUCT_NAME "ALAP Opto Tests"
        COMPANY_NAME "Al Learned Audio Productions")

    juce_generate_juce_header(ALAPOptoTests)

    # Percorso d'uscita fisso e nome senza spazi:
    #   build/tests/Release/ALAPOptoTests.exe
    set_target_properties(ALAPOptoTests PROPERTIES
        OUTPUT_NAME              "ALAPOptoTests"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")

    target_sources(ALAPOptoTests
        PRIVATE
            Tests/TestMain.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/PresetManager.cpp)

    target_include_directories(ALAPOptoTests PRIVATE Source)

    target_compile_definitions(ALAPOptoTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)

    target_link_libraries(ALAPOptoTests
        PRIVATE
            ALAPOptoAssets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
