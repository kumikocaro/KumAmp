cmake_minimum_required(VERSION 3.24)

# ---------------------------------------------------------------------------
# Plataforma: macOS, Apple Silicon nativo por defecto.
# Estas variables deben fijarse ANTES de project().
# ---------------------------------------------------------------------------
option(KUMAMP_UNIVERSAL "Compilar binario universal (arm64;x86_64) en lugar de arm64 nativo" OFF)

if(KUMAMP_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Version minima de macOS")

project(KumAmp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(KUMAMP_BUILD_TESTS       "Compilar los tests del nucleo DSP (sin JUCE)" ON)
option(KUMAMP_COPY_AFTER_BUILD  "Copiar VST3/AU a ~/Library/Audio/Plug-Ins tras compilar" ON)
set(KUMAMP_JUCE_PATH "" CACHE PATH "Checkout local de JUCE (vacio = descargar con FetchContent)")

# ---------------------------------------------------------------------------
# Flags comunes de optimizacion para el nucleo DSP.
# Sin -ffast-math: rompe isnan/isinf y la deteccion de denormales.
# ---------------------------------------------------------------------------
add_library(kumamp_compile_flags INTERFACE)
target_compile_options(kumamp_compile_flags INTERFACE
    -Wall -Wextra -Wshadow -Wno-unused-parameter
    $<$<CONFIG:Release,RelWithDebInfo>:-O3 -fno-math-errno -ffp-contract=fast>)
if(NOT KUMAMP_UNIVERSAL)
    # Afinado especifico para la microarquitectura Apple (M1 y posteriores).
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
    Source/DSP/Routing/RoutingParams.cpp
    Source/DSP/Pitch/WsolaPitchShifter.cpp
    Source/DSP/Pitch/PitchShifterBlock.cpp
    Source/DSP/Utility/VolumeBlock.cpp)
target_include_directories(KumAmpCore PUBLIC Source)
target_link_libraries(KumAmpCore PRIVATE kumamp_compile_flags)
set_target_properties(KumAmpCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
if(KUMAMP_JUCE_PATH)
    add_subdirectory(${KUMAMP_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(KumAmp
    PRODUCT_NAME                  "KumAmp"
    COMPANY_NAME                  "KumAmp Audio"
    BUNDLE_ID                     com.kumampaudio.kumamp
    PLUGIN_MANUFACTURER_CODE      Kumo
    PLUGIN_CODE                   KAmp
    FORMATS                       VST3 AU Standalone
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
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
    Source/Engine/GridEngine.cpp
    Source/UI/GridView.cpp
    Source/UI/BlockPanel.cpp)

target_compile_definitions(KumAmp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(KumAmp
    PRIVATE
        KumAmpCore
        kumamp_compile_flags
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests (solo nucleo, sin JUCE)
# ---------------------------------------------------------------------------
if(KUMAMP_BUILD_TESTS)
    enable_testing()
    add_executable(KumAmpTests Tests/CoreTests.cpp)
    target_link_libraries(KumAmpTests PRIVATE KumAmpCore kumamp_compile_flags)
    add_test(NAME KumAmpCoreTests COMMAND KumAmpTests)
endif()
