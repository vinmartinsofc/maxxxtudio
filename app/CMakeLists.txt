set(MAXXXTUDIO_APP_ICON_ARGS)
if(NOT WIN32)
    list(APPEND MAXXXTUDIO_APP_ICON_ARGS
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/maxxx-tudio.png"
        ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/maxxx-tudio.png")
endif()

juce_add_gui_app(MaXXXtudio
    PRODUCT_NAME "MaXXXtudio"
    COMPANY_NAME "rjc"
    ${MAXXXTUDIO_APP_ICON_ARGS}
    VERSION ${PROJECT_VERSION})

if(WIN32)
    configure_file (MaXXXtudioIcon.rc.in
                    "${CMAKE_CURRENT_BINARY_DIR}/MaXXXtudioIcon.rc" @ONLY)
    target_sources (MaXXXtudio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/MaXXXtudioIcon.rc")
endif()

file(GLOB_RECURSE MAXXXTUDIO_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
list(APPEND MAXXXTUDIO_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app/maxxx-tudio.png")
juce_add_binary_data(MaXXXtudioIcons SOURCES ${MAXXXTUDIO_ICON_FILES})

target_sources(MaXXXtudio PRIVATE
    Main.cpp
    Icons.cpp
    AppModalDialog.cpp
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
    ReferenceMasterWindow.cpp
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

target_compile_definitions(MaXXXtudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MaXXXtudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MaXXXtudio,JUCE_VERSION>")

target_link_libraries(MaXXXtudio
    PRIVATE
        roll_core
        MaXXXtudioIcons
        juce::juce_data_structures
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_link_options(MaXXXtudio PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/MaXXXtudio.manifest")
endif()

juce_add_gui_app(MaXXXtudioPluginWorker
    PRODUCT_NAME "MaXXXtudio Plugin Worker"
    COMPANY_NAME "rjc"
    VERSION ${PROJECT_VERSION})

target_sources(MaXXXtudioPluginWorker PRIVATE
    PluginWorkerMain.cpp
    PluginWorkerRuntime.cpp
    PluginScanRuntime.cpp)

if(WIN32)
    target_sources(MaXXXtudioPluginWorker PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/MaXXXtudioIcon.rc")
endif()

target_compile_definitions(MaXXXtudioPluginWorker PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MaXXXtudioPluginWorker,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MaXXXtudioPluginWorker,JUCE_VERSION>")

target_link_libraries(MaXXXtudioPluginWorker
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_link_options(MaXXXtudioPluginWorker PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/MaXXXtudio.manifest")
endif()

set_target_properties(MaXXXtudioPluginWorker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app/MaXXXtudio_artefacts/$<CONFIG>")

target_compile_definitions(MaXXXtudio PRIVATE
    MAXXXTUDIO_PLUGIN_WORKER_PATH="$<TARGET_FILE:MaXXXtudioPluginWorker>")

if(WIN32)
    target_link_libraries(MaXXXtudio PRIVATE shell32)
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
            MaXXXtudioIcons
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
            MaXXXtudioIcons
            Catch2::Catch2WithMain
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME TaskRegistryTests COMMAND task_registry_tests)

    add_executable(audio_engine_tests
        ../tests/AudioEngineTests.cpp
        AudioEngine.cpp
        ReferenceMasterWindow.cpp
        RemotePluginProcessor.cpp
        DrumRackProcessor.cpp
        DrumRackEditor.cpp
        ThemeKnobSlider.cpp
        AppModalDialog.cpp
        NativeEffectProcessors.cpp)
    target_include_directories(audio_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(audio_engine_tests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_VST3=1
        MAXXXTUDIO_PLUGIN_WORKER_PATH="$<TARGET_FILE:MaXXXtudioPluginWorker>")
    target_link_libraries(audio_engine_tests
        PRIVATE
            roll_core
            Catch2::Catch2WithMain
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors
            juce::juce_dsp
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
