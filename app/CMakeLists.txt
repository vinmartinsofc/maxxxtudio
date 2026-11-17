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
    DrumRackProcessor.cpp
    DrumRackEditor.cpp
    OptionsWindow.cpp
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
    ProjectFile.cpp
    GrooveLibrary.cpp
    GrooveBrowser.cpp)

target_compile_definitions(ProjectFreedom PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0
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

if(WIN32)
    target_link_libraries(ProjectFreedom PRIVATE shell32)
endif()

if(ROLL_BUILD_TESTS)
    add_executable(piano_roll_tests
        ../tests/PianoRollTests.cpp
        Icons.cpp
        PianoRollEditor.cpp
        RollModel.cpp)
    target_include_directories(piano_roll_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(piano_roll_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(piano_roll_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            ProjectFreedomIcons
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME PianoRollTests COMMAND piano_roll_tests)

    add_executable(project_model_tests
        ../tests/ProjectModelTests.cpp
        ProjectModel.cpp)
    target_include_directories(project_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(project_model_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(project_model_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME ProjectModelTests COMMAND project_model_tests)

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

    add_executable(audio_engine_tests
        ../tests/AudioEngineTests.cpp
        AudioEngine.cpp
        DrumRackProcessor.cpp
        DrumRackEditor.cpp)
    target_include_directories(audio_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(audio_engine_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(audio_engine_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME AudioEngineTests COMMAND audio_engine_tests)

    add_executable(project_file_tests
        ../tests/ProjectFileTests.cpp
        ProjectFile.cpp)
    target_include_directories(project_file_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(project_file_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1)
    target_link_libraries(project_file_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME ProjectFileTests COMMAND project_file_tests)

    add_executable(groove_library_tests
        ../tests/GrooveLibraryTests.cpp
        GrooveLibrary.cpp)
    target_include_directories(groove_library_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(groove_library_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(groove_library_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME GrooveLibraryTests COMMAND groove_library_tests)
endif()
