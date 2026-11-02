cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# Plataformas: macOS (Apple Silicon nativo por defecto) y Windows x64 (MSVC).
# Estas variables deben fijarse ANTES de project().
# ---------------------------------------------------------------------------
option(KUMAMP_UNIVERSAL "macOS: binario universal (arm64;x86_64) en lugar de arm64 nativo" OFF)

if(APPLE)
    if(KUMAMP_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Version minima de macOS")
endif()

# Windows: runtime de MSVC estatico -> el usuario no necesita instalar el VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(KumAmp VERSION 0.1.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(KUMAMP_BUILD_TESTS       "Compilar los tests del nucleo DSP (sin JUCE)" ON)
# En Windows copiar a "Program Files\Common Files\VST3" exige permisos de administrador:
# alli se instala con el instalador (installer/KumAmp.iss).
if(APPLE)
    option(KUMAMP_COPY_AFTER_BUILD "Copiar VST3/AU a ~/Library/Audio/Plug-Ins tras compilar" ON)
else()
    option(KUMAMP_COPY_AFTER_BUILD "Copiar el VST3 a la carpeta del sistema tras compilar" OFF)
endif()
set(KUMAMP_JUCE_PATH "" CACHE PATH "Checkout local de JUCE (vacio = descargar con FetchContent)")

# ---------------------------------------------------------------------------
# Flags comunes de optimizacion para el nucleo DSP.
# Sin -ffast-math: rompe isnan/isinf y la deteccion de denormales.
# ---------------------------------------------------------------------------
add_library(kumamp_compile_flags INTERFACE)
if(MSVC)
    # /utf-8: fuentes y literales en UTF-8. /bigobj: Eigen / plantillas grandes.
    target_compile_options(kumamp_compile_flags INTERFACE /W3 /utf-8 /bigobj /permissive- $<$<CONFIG:Release,RelWithDebInfo>:/O2>)
    target_compile_definitions(kumamp_compile_flags INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
else()
    target_compile_options(kumamp_compile_flags INTERFACE
        -Wall -Wextra -Wshadow -Wno-unused-parameter
        $<$<CONFIG:Release,RelWithDebInfo>:-O3 -fno-math-errno -ffp-contract=fast>)
endif()

# Afinado para la microarquitectura Apple (M1 y posteriores) en builds arm64 nativos.
set(KUMAMP_APPLE_NATIVE OFF)
if(APPLE AND NOT KUMAMP_UNIVERSAL)
    set(KUMAMP_APPLE_NATIVE ON)
    target_compile_options(kumamp_compile_flags INTERFACE -mcpu=apple-m1)
endif()

# ---------------------------------------------------------------------------
# Nucleo DSP independiente de JUCE (grid, escenas, pitch shifter).
# Se puede testear y perfilar sin el framework.
# ---------------------------------------------------------------------------
add_library(KumAmpCore STATIC
    Source/Graph/BlockTypes.cpp
    Source/Graph/ParamStore.cpp
    Source/Graph/DSPBlock.cpp
    Source/Graph/GridModel.cpp
    Source/Graph/RenderPlan.cpp
    Source/Graph/BlockFactory.cpp
    Source/Scenes/SceneBank.cpp
    Source/Core/PitchDetector.cpp
    Source/DSP/Routing/RoutingParams.cpp
    Source/DSP/Pitch/WsolaPitchShifter.cpp
    Source/DSP/Pitch/PitchShifterBlock.cpp
    Source/DSP/Utility/VolumeBlock.cpp
    Source/DSP/Pitch/PreFxBlocks.cpp
    Source/DSP/Pitch/GuitarSynthBlock.cpp
    Source/DSP/Dynamics/DynamicsBlocks.cpp
    Source/DSP/Modulation/ModulationBlocks.cpp
    Source/DSP/Time/DelayBlocks.cpp
    Source/DSP/Time/ReverbBlocks.cpp)
target_include_directories(KumAmpCore PUBLIC Source)
target_link_libraries(KumAmpCore PRIVATE kumamp_compile_flags)
set_target_properties(KumAmpCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
include(FetchContent)

if(KUMAMP_JUCE_PATH)
    add_subdirectory(${KUMAMP_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# NeuralAmpModelerCore (MIT): inferencia NAM, archivos .nam 0.5 - 0.7 (A1 / A2).
# Se compila como OBJECT library: las arquitecturas se registran con
# inicializadores estaticos que una libreria estatica podria descartar.
# ---------------------------------------------------------------------------
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "Dependencies/eigen"
    SOURCE_SUBDIR  __kumamp_no_cmake__)
FetchContent_MakeAvailable(NeuralAmpModelerCore)

# ---------------------------------------------------------------------------
# Steinberg ASIO SDK (GPLv3 option of its dual license): ASIO drivers in the
# Windows standalone. Only iasiodrv.h and the headers it pulls in are used.
# ---------------------------------------------------------------------------
option(KUMAMP_ASIO "Soporte ASIO en el standalone de Windows (SDK de Steinberg, GPLv3)" ${WIN32})
if(KUMAMP_ASIO)
    FetchContent_Declare(AsioSdk
        URL      https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
        URL_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
        SOURCE_SUBDIR __kumamp_no_cmake__)
    FetchContent_MakeAvailable(AsioSdk)
    if(NOT EXISTS ${asiosdk_SOURCE_DIR}/common/iasiodrv.h)
        message(FATAL_ERROR "ASIO SDK sin common/iasiodrv.h en ${asiosdk_SOURCE_DIR}")
    endif()
endif()

set(NAM_DIR ${neuralampmodelercore_SOURCE_DIR})
file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)
add_library(KumAmpNam OBJECT ${NAM_SOURCES})
target_include_directories(KumAmpNam SYSTEM PUBLIC
    ${NAM_DIR} ${NAM_DIR}/Dependencies/eigen ${NAM_DIR}/Dependencies/nlohmann)
target_compile_definitions(KumAmpNam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
if(MSVC)
    target_compile_definitions(KumAmpNam PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    target_compile_options(KumAmpNam PRIVATE /W0 /utf-8 /bigobj $<$<CONFIG:Release,RelWithDebInfo>:/O2>)
else()
    target_compile_options(KumAmpNam PRIVATE -w $<$<CONFIG:Release,RelWithDebInfo>:-O3>)
endif()
if(KUMAMP_APPLE_NATIVE)
    target_compile_options(KumAmpNam PRIVATE -mcpu=apple-m1)
endif()
set_target_properties(KumAmpNam PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fuentes que dependen de JUCE y/o NAM. Se compilan en el plugin y en los
# tests de integracion (los modulos JUCE no se pueden compartir entre targets).
set(KUMAMP_JUCE_SOURCES
    Source/Engine/GridEngine.cpp
    Source/DSP/JuceBlocks.cpp
    Source/DSP/Amp/NamAmpBlock.cpp
    Source/DSP/Amp/NamProfileInfo.cpp
    Source/DSP/Cab/IrLoaderBlock.cpp
    Source/Tuner/Tuner.cpp
    Source/Presets/PresetManager.cpp
    Source/Tone3000/Tone3000.cpp
    Source/UI/TunerView.cpp)

set(KUMAMP_UI_SOURCES
    Source/UI/Theme.cpp
    Source/UI/KumAmpLookAndFeel.cpp
    Source/UI/EditorSections.cpp
    Source/UI/GridView.cpp
    Source/UI/BlockPanel.cpp
    Source/UI/EqView.cpp
    Source/UI/SettingsView.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/Tone3000Picker.cpp)

# AU solo existe en macOS.
if(APPLE)
    set(KUMAMP_FORMATS VST3 AU Standalone)
else()
    set(KUMAMP_FORMATS VST3 Standalone)
endif()

juce_add_plugin(KumAmp
    PRODUCT_NAME                  "KumAmp"
    COMPANY_NAME                  "KumAmp Audio"
    BUNDLE_ID                     com.kumampaudio.kumamp
    PLUGIN_MANUFACTURER_CODE      Kumo
    PLUGIN_CODE                   KAmp
    FORMATS                       ${KUMAMP_FORMATS}
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              TRUE     # Program Change -> scenes, CC -> macros
    NEEDS_MIDI_OUTPUT             TRUE     # guitar -> MIDI notes for other instruments
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE
    AU_MAIN_TYPE                  kAudioUnitType_Effect
    VST3_CATEGORIES               Fx Distortion
    HARDENED_RUNTIME_ENABLED      TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "KumAmp necesita la entrada de audio para procesar tu guitarra."
    COPY_PLUGIN_AFTER_BUILD       ${KUMAMP_COPY_AFTER_BUILD})

target_sources(KumAmp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${KUMAMP_JUCE_SOURCES}
    ${KUMAMP_UI_SOURCES})

target_compile_definitions(KumAmp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# TONE3000 API publishable key (t3k_pub_...): the OAuth client_id, safe to ship
# in a client. The secret key (t3k_cs_...) must never be put here.
set(KUMAMP_T3K_PUBLISHABLE_KEY "t3k_pub_BB7N5zgwXuDL_tjPaNwT4_fE5t1D91JI" CACHE STRING "Clave publica de la API de TONE3000")
set_property(SOURCE Source/Tone3000/Tone3000.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    KUMAMP_T3K_PUBLISHABLE_KEY="${KUMAMP_T3K_PUBLISHABLE_KEY}")

if(KUMAMP_ASIO)
    target_compile_definitions(KumAmp PUBLIC JUCE_ASIO=1)
    target_include_directories(KumAmp SYSTEM PRIVATE ${asiosdk_SOURCE_DIR}/common)
endif()

target_link_libraries(KumAmp
    PRIVATE
        KumAmpCore
        KumAmpNam
        kumamp_compile_flags
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# El codigo compartido del plugin es una libreria estatica: el linker de cada
# formato descartaria los objetos NAM que solo contienen registros estaticos
# de arquitecturas (WaveNet, LSTM, A2...). Se enlazan directamente.
foreach(format VST3 AU Standalone)
    if(TARGET KumAmp_${format})
        target_sources(KumAmp_${format} PRIVATE $<TARGET_OBJECTS:KumAmpNam>)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Tests (solo nucleo, sin JUCE)
# ---------------------------------------------------------------------------
if(KUMAMP_BUILD_TESTS)
    enable_testing()
    add_executable(KumAmpTests Tests/CoreTests.cpp)
    target_link_libraries(KumAmpTests PRIVATE KumAmpCore kumamp_compile_flags)
    add_test(NAME KumAmpCoreTests COMMAND KumAmpTests)

    # Integracion: motor + NAM + IR con JUCE real.
    juce_add_console_app(KumAmpIntegrationTests PRODUCT_NAME "KumAmpIntegrationTests")
    target_sources(KumAmpIntegrationTests PRIVATE Tests/IntegrationTests.cpp ${KUMAMP_JUCE_SOURCES} ${KUMAMP_UI_SOURCES})
    target_compile_definitions(KumAmpIntegrationTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        KUMAMP_NAM_EXAMPLES="${NAM_DIR}/example_models"
        KUMAMP_SNAPSHOT_DIR="${CMAKE_BINARY_DIR}/snapshots")
    target_link_libraries(KumAmpIntegrationTests PRIVATE
        KumAmpCore KumAmpNam kumamp_compile_flags
        juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_data_structures juce::juce_gui_basics
        juce::juce_cryptography
        juce::juce_recommended_config_flags)
    add_test(NAME KumAmpIntegrationTests COMMAND KumAmpIntegrationTests)

    # Carga el VST3 compilado como un DAW y comprueba que un .nam procesa audio.
    juce_add_console_app(KumAmpHostTest PRODUCT_NAME "KumAmpHostTest")
    target_sources(KumAmpHostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(KumAmpHostTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
        KUMAMP_NAM_EXAMPLES="${NAM_DIR}/example_models"
        KUMAMP_VST3_PATH="$<GENEX_EVAL:$<TARGET_PROPERTY:KumAmp_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
    target_link_libraries(KumAmpHostTest PRIVATE
        kumamp_compile_flags juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_dependencies(KumAmpHostTest KumAmp_VST3)
    add_test(NAME KumAmpHostTest COMMAND KumAmpHostTest)

    # Renders the real editor (processor + UI) to PNG for visual review.
    juce_add_console_app(KumAmpUiSnapshot PRODUCT_NAME "KumAmpUiSnapshot")
    target_sources(KumAmpUiSnapshot PRIVATE Tests/UiSnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        ${KUMAMP_JUCE_SOURCES} ${KUMAMP_UI_SOURCES})
    target_compile_definitions(KumAmpUiSnapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="KumAmp"
        KUMAMP_NAM_EXAMPLES="${NAM_DIR}/example_models"
        KUMAMP_SNAPSHOT_DIR="${CMAKE_BINARY_DIR}/snapshots")
    target_link_libraries(KumAmpUiSnapshot PRIVATE
        KumAmpCore KumAmpNam kumamp_compile_flags
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
endif()
