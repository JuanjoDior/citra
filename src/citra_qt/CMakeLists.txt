set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)
cmake_policy(SET CMP0071 NEW)

add_executable(citra-qt
    Info.plist
    aboutdialog.cpp
    aboutdialog.h
    bootmanager.cpp
    bootmanager.h
    cheat_gui.cpp
    cheat_gui.h
    cheatsearch.cpp
    cheatsearch.h
    camera/camera_util.cpp
    camera/camera_util.h
    camera/still_image_camera.cpp
    camera/still_image_camera.h
    camera/qt_camera_base.cpp
    camera/qt_camera_base.h
    camera/qt_multimedia_camera.cpp
    camera/qt_multimedia_camera.h
    citra-qt.rc
    camera/still_image_camera.cpp
    camera/still_image_camera.h
    configuration/config.cpp
    configuration/config.h
    configuration/configure_audio.cpp
    configuration/configure_audio.h
    configuration/configure_camera.cpp
    configuration/configure_camera.h
    configuration/configure_dialog.cpp
    configuration/configure_dialog.h
    configuration/configure_general.cpp
    configuration/configure_general.h
    configuration/configure_graphics.cpp
    configuration/configure_graphics.h
    configuration/configure_input.cpp
    configuration/configure_input.h
    configuration/configure_motion_touch.cpp
    configuration/configure_motion_touch.h
    configuration/configure_system.cpp
    configuration/configure_system.h
    configuration/configure_hacks.cpp
    configuration/configure_hacks.h
    multiplayer/ip_connect.cpp
    multiplayer/ip_connect.h
    multiplayer/host_room.cpp
    multiplayer/host_room.h
    multiplayer/message.cpp
    multiplayer/message.h
    multiplayer/state.cpp
    multiplayer/state.h
    multiplayer/validation.h
    game_list.cpp
    game_list.h
    game_list_p.h
    hotkeys.cpp
    hotkeys.h
    main.cpp
    main.h
    ui_settings.cpp
    ui_settings.h
    util/console.cpp
    util/console.h
    util/util.cpp
    util/util.h
    swkbd.cpp
    swkbd.h
    control_panel.cpp
    control_panel.h
)

set(UIS
    configuration/configure.ui
    configuration/configure_audio.ui
    configuration/configure_camera.ui
    configuration/configure_general.ui
    configuration/configure_graphics.ui
    configuration/configure_input.ui
    configuration/configure_motion_touch.ui
    configuration/configure_system.ui
    configuration/configure_hacks.ui
    swkbd.ui
    aboutdialog.ui
    hotkeys.ui
    cheat_gui.ui
    cheatsearch.ui
    main.ui
    control_panel.ui
    multiplayer/ip_connect.ui
    multiplayer/host_room.ui
)

file(GLOB_RECURSE ICONS ${CMAKE_SOURCE_DIR}/dist/icons/*)
file(GLOB_RECURSE THEMES ${CMAKE_SOURCE_DIR}/dist/qt_themes/*)

qt5_wrap_ui(UI_HDRS ${UIS})

if (ENABLE_QT_TRANSLATION)
    set(CITRA_QT_LANGUAGES "${CMAKE_SOURCE_DIR}/dist/languages" CACHE PATH "Path to the translation bundle for the Qt frontend")
    option(GENERATE_QT_TRANSLATION "Generate en.ts as the translation source file" OFF)

    # Update source TS file if enabled
    if (GENERATE_QT_TRANSLATION)
        get_target_property(SRCS citra-qt SOURCES)
        qt5_create_translation(QM_FILES ${SRCS} ${UIS} ${CITRA_QT_LANGUAGES}/en.ts)
        add_custom_target(translation ALL DEPENDS ${CITRA_QT_LANGUAGES}/en.ts)
    endif()

    # Find all TS files except en.ts
    file(GLOB_RECURSE LANGUAGES_TS ${CITRA_QT_LANGUAGES}/*.ts)
    list(REMOVE_ITEM LANGUAGES_TS ${CITRA_QT_LANGUAGES}/en.ts)

    # Compile TS files to QM files
    qt5_add_translation(LANGUAGES_QM ${LANGUAGES_TS})

    # Build a QRC file from the QM file list
    set(LANGUAGES_QRC ${CMAKE_CURRENT_BINARY_DIR}/languages.qrc)
    file(WRITE ${LANGUAGES_QRC} "<RCC><qresource prefix=\"languages\">\n")
    foreach (QM ${LANGUAGES_QM})
        get_filename_component(QM_FILE ${QM} NAME)
        file(APPEND ${LANGUAGES_QRC} "<file>${QM_FILE}</file>\n")
    endforeach (QM)
    file(APPEND ${LANGUAGES_QRC} "</qresource></RCC>")

    # Add the QRC file to package in all QM files
    qt5_add_resources(LANGUAGES ${LANGUAGES_QRC})
else()
    set(LANGUAGES)
endif()

target_sources(citra-qt
    PRIVATE
        ${ICONS}
        ${THEMES}
        ${UI_HDRS}
        ${UIS}
        ${LANGUAGES}
)

if (APPLE)
    set(MACOSX_ICON "../../dist/citra.icns")
    set_source_files_properties(${MACOSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(citra-qt PRIVATE ${MACOSX_ICON})
    set_target_properties(citra-qt PROPERTIES MACOSX_BUNDLE TRUE)
    set_target_properties(citra-qt PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)
elseif(WIN32)
    # compile as a win32 gui application instead of a console application
    target_link_libraries(citra-qt PRIVATE Qt5::WinMain)
    if(MSVC)
        set_target_properties(citra-qt PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    elseif(MINGW)
        set_target_properties(citra-qt PROPERTIES LINK_FLAGS_RELEASE "-mwindows")
    endif()
endif()

create_target_directory_groups(citra-qt)

target_link_libraries(citra-qt PRIVATE audio_core common core input_common network video_core)
target_link_libraries(citra-qt PRIVATE Boost::boost glad nihstro-headers Qt5::OpenGL Qt5::Widgets Qt5::Multimedia)
if (ENABLE_DISCORD_RPC)
    target_link_libraries(citra-qt PRIVATE discord-rpc)
endif()
target_link_libraries(citra-qt PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS citra-qt RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if (MSVC)
    include(CopyCitraQt5Deps)
    include(CopyCitraSDLDeps)
    copy_citra_Qt5_deps(citra-qt)
    copy_citra_SDL_deps(citra-qt)
endif()
