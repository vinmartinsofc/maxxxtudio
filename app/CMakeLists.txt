set(PROJECT_FREEDOM_APP_ICON_ARGS)
if(NOT WIN32)
    list(APPEND PROJECT_FREEDOM_APP_ICON_ARGS
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/project-freedom.png"
        ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/project-freedom.png")
endif()

juce_add_gui_app(ProjectFreedom
    PRODUCT_NAME "Project Freedom"
    COMPANY_NAME "rjc"
    ${PROJECT_FREEDOM_APP_ICON_ARGS}
    VERSION ${PROJECT_VERSION})

if(WIN32)
    configure_file (ProjectFreedomIcon.rc.in
                    "${CMAKE_CURRENT_BINARY_DIR}/ProjectFreedomIcon.rc" @ONLY)
    target_sources (ProjectFreedom PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ProjectFreedomIcon.rc")
endif()

file(GLOB_RECURSE PROJECT_FREEDOM_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
list(APPEND PROJECT_FREEDOM_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/project-freedom.png")
juce_add_binary_data(ProjectFreedomIcons SOURCES ${PROJECT_FREEDOM_ICON_FILES})

target_sources(ProjectFreedom PRIVATE
    Main.cpp
    Icons.cpp
    FreedomModalDialog.cpp
    ProjectModel.cpp
    ArrangementView.cpp
    DeviceView.cpp
    AudioEngine.cpp
    RemotePluginProcessor.cpp
    DrumRackProcessor.cpp
    DrumRackEditor.cpp
    NativeEffectProcessors.cpp
    OptionsWindow.cpp
    AudioClipEditor.cpp
    ThemeKnobSlider.cpp
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
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
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

if(MSVC)
    target_link_options(ProjectFreedom PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/ProjectFreedom.manifest")
endif()

juce_add_gui_app(ProjectFreedomPluginWorker
    PRODUCT_NAME "Project Freedom Plugin Worker"
    COMPANY_NAME "rjc"
    VERSION ${PROJECT_VERSION})

target_sources(ProjectFreedomPluginWorker PRIVATE
    PluginWorkerMain.cpp
    PluginWorkerRuntime.cpp
    PluginScanRuntime.cpp)

if(WIN32)
    target_sources(ProjectFreedomPluginWorker PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/ProjectFreedomIcon.rc")
endif()

target_compile_definitions(ProjectFreedomPluginWorker PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ProjectFreedomPluginWorker,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ProjectFreedomPluginWorker,JUCE_VERSION>")

target_link_libraries(ProjectFreedomPluginWorker
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_link_options(ProjectFreedomPluginWorker PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/ProjectFreedom.manifest")
endif()

set_target_properties(ProjectFreedomPluginWorker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app/ProjectFreedom_artefacts/$<CONFIG>")

target_compile_definitions(ProjectFreedom PRIVATE
    PROJECT_FREEDOM_PLUGIN_WORKER_PATH="$<TARGET_FILE:ProjectFreedomPluginWorker>")

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
        Icons.cpp
        TaskRegistry.cpp)
    target_include_directories(task_registry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(task_registry_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(task_registry_tests
        PRIVATE
            roll_core
            ProjectFreedomIcons
            Catch2::Catch2WithMain
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME TaskRegistryTests COMMAND task_registry_tests)

    add_executable(audio_engine_tests
        ../tests/AudioEngineTests.cpp
        AudioEngine.cpp
        RemotePluginProcessor.cpp
        DrumRackProcessor.cpp
        DrumRackEditor.cpp
        FreedomModalDialog.cpp
        NativeEffectProcessors.cpp)
    target_include_directories(audio_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(audio_engine_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_VST3=1
        PROJECT_FREEDOM_PLUGIN_WORKER_PATH="$<TARGET_FILE:ProjectFreedomPluginWorker>")
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
