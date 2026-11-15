juce_add_gui_app(ProjectFreedom
    PRODUCT_NAME "Project Freedom"
    COMPANY_NAME "rjc"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/project-freedom.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/project-freedom.png"
    VERSION ${PROJECT_VERSION})

file(GLOB_RECURSE PROJECT_FREEDOM_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
juce_add_binary_data(ProjectFreedomIcons SOURCES ${PROJECT_FREEDOM_ICON_FILES})

target_sources(ProjectFreedom PRIVATE
    Main.cpp
    Icons.cpp
    ProjectModel.cpp
    ArrangementView.cpp
    DeviceView.cpp
    AudioEngine.cpp
    AudioClipEditor.cpp
    AudioFileBrowser.cpp
    PluginScanner.cpp
    BrowserPanel.cpp
    TransportBar.cpp
    TaskRegistry.cpp
    ActivityIndicator.cpp
    TaskStatusBar.cpp
    RollModel.cpp
    PianoRollEditor.cpp
    MainComponent.cpp
    ProjectFile.cpp)

target_compile_definitions(ProjectFreedom PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ProjectFreedom,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ProjectFreedom,JUCE_VERSION>")

target_link_libraries(ProjectFreedom
    PRIVATE
        roll_core
        ProjectFreedomIcons
        juce::juce_data_structures
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_gui_basics
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(ROLL_BUILD_TESTS)
    add_executable(task_registry_tests
        ../tests/TaskRegistryTests.cpp
        ActivityIndicator.cpp
        DeviceView.cpp
        TaskRegistry.cpp)
    target_include_directories(task_registry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(task_registry_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(task_registry_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME TaskRegistryTests COMMAND task_registry_tests)
endif()
