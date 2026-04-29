cmake_minimum_required(VERSION 3.22)

project(BassEnhancer VERSION 1.0.0)

# =============================================================================
# JUCE path — points to your JUCE installation
# =============================================================================
add_subdirectory(/Users/gerardo/Downloads/JUCE JUCE)

# =============================================================================
# Plugin target
# =============================================================================
juce_add_plugin(BassEnhancer
    PLUGIN_MANUFACTURER_CODE      Bsnh
    PLUGIN_CODE                   BsEn
    FORMATS                       VST3 Standalone
    PRODUCT_NAME                  "Bass Enhancer"
    COMPANY_NAME                  "Gerardo"
    BUNDLE_ID                     "com.yourstudio.bassenhancer"

    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
    IS_MIDI_EFFECT                FALSE

    PLUGIN_VST3_CATEGORY          "Fx"
    VST3_CATEGORIES               "Fx" "Dynamics" "EQ"

    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE
    COPY_PLUGIN_AFTER_BUILD       FALSE
)

# =============================================================================
# Source files
# =============================================================================
target_sources(BassEnhancer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/FeatureExtractor.cpp
    Source/FeatureExtractor.h
    Source/DecisionEngine.cpp
    Source/DecisionEngine.h
    Source/AdaptiveProcessor.cpp
    Source/AdaptiveProcessor.h
)

# =============================================================================
# Compile definitions
# =============================================================================
target_compile_definitions(BassEnhancer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# =============================================================================
# JUCE module dependencies
# =============================================================================
target_link_libraries(BassEnhancer
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# =============================================================================
# C++ standard
# =============================================================================
target_compile_features(BassEnhancer PRIVATE cxx_std_17)

# =============================================================================
# macOS — Universal binary (Apple Silicon + Intel)
# =============================================================================
set_target_properties(BassEnhancer PROPERTIES
    OSX_ARCHITECTURES "arm64;x86_64"
)
