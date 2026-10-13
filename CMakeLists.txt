cmake_minimum_required(VERSION 3.19)  # for string(JSON)
project(Red_Panda_CPP
    LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(CTest)
# a phony target that depends on all test targets
# usage:
#   make all-test-targets
#   make test
add_custom_target(all-test-targets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

#set(CMAKE_VERBOSE_MAKEFILE ON)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(MACOS 1)
endif()

option(FORCE_QT5 "Force Qt 5 when building with system Qt" OFF)
option(LUA_ADDON "Enable Lua add-ons" OFF)
option(SDCC "Enable SDCC compiler support" ON)
option(VCS "Enable Git integration" ON)
option(WINDOWS_PREFER_OPENCONSOLE "Prefer OpenConsole.exe as terminal emulator" OFF)

if (WIN32)
    set(FILESYSTEM_LAYOUT_DEFAULT flat)
    set(MAKE_INTERFACE_DEFAULT mingw32)
    set(PORTABLE_CONFIG_DEFAULT registry)
elseif (MACOS)
    set(FILESYSTEM_LAYOUT_DEFAULT bundle)
    set(MAKE_INTERFACE_DEFAULT unix)
    set(PORTABLE_CONFIG_DEFAULT non)
else()
    set(FILESYSTEM_LAYOUT_DEFAULT hierarchy)
    set(MAKE_INTERFACE_DEFAULT unix)
    set(PORTABLE_CONFIG_DEFAULT non)
endif()

set(APP_NAME RedPandaRust CACHE STRING "Subdirectory name to $prefix/share and $prefix/libexec.")
set(FILESYSTEM_LAYOUT ${FILESYSTEM_LAYOUT_DEFAULT} CACHE STRING "Use 'hierarchy', 'flat', or 'bundle' filesystem layout.")
set(LIBEXECDIR libexec CACHE STRING "Directory for auxiliary executables, RELATIVE TO prefix.")
set(MAKE_INTERFACE ${MAKE_INTERFACE_DEFAULT} CACHE STRING "Use make interface of 'unix' (system `make`, SHELL=sh) or 'mingw32' (toolchain `mingw32-make.exe`, SHELL=cmd).")
set(OVERRIDE_MALLOC "" CACHE STRING "Override malloc implementation, e.g. mimalloc")
set(PORTABLE_CONFIG ${PORTABLE_CONFIG_DEFAULT} CACHE STRING
    # avoid "yes" "no", they may be treated as boolean values
    "Use portable configuration directory. 'oui' (for yes), 'non' (for no), or 'registry'.")

set(QT_COMPONENTS
    Core
    Concurrent
    Gui
    LinguistTools
    Network
    PrintSupport
    Svg
    Test
    Widgets
    Xml)

if (MACOS)
    list(APPEND QT_COMPONENTS GuiPrivate)
endif()

if (FORCE_QT5)
    set(QT_VERSION_MAJOR 5)
    find_package(Qt5 5.15 REQUIRED COMPONENTS ${QT_COMPONENTS})
else()
    find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})
    if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
        find_package(Qt6 6.8 REQUIRED COMPONENTS ${QT_COMPONENTS})
    else()
        find_package(Qt5 5.15 REQUIRED COMPONENTS ${QT_COMPONENTS})
    endif()
endif()

if (${QT_VERSION_MAJOR} LESS 6)
    # a phony target that depends on all lupdate targets (Qt 5)
    # the actual lupdate targets will be added in target_embed_translations function
    add_custom_target(update_translations)

    # Qt 5 doesn't define QT5_INSTALL_PREFIX, QT_FEATURE_static for CMake
    get_target_property(Qt5Core_loc Qt5::Core LOCATION_RELEASE)  # C:/Qt/5.15.15+redpanda0/64-ucrt/lib/libQt5Core.a
    get_filename_component(QT5_INSTALL_PREFIX "${Qt5Core_loc}/../.." ABSOLUTE)  # C:/Qt/5.15.15+redpanda0/64-ucrt
    get_target_property(Qt5Core_type Qt5::Core TYPE)
    if(Qt5Core_type STREQUAL "STATIC_LIBRARY")
        set(QT_FEATURE_static ON)
    endif()
endif()

# read version from "version.json"
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/version.json")
file(READ version.json VERSION_JSON)
string(JSON APP_VERSION_MAJOR GET ${VERSION_JSON} major)
string(JSON APP_VERSION_MINOR GET ${VERSION_JSON} minor)
string(JSON APP_VERSION_PATCH GET ${VERSION_JSON} patch)
string(JSON APP_VERSION_PRE_RELEASE GET ${VERSION_JSON} preRelease)

# Published Rust versions are reproducible across source archives and Git clones.
set(APP_VERSION_BUILD "")

if (APP_VERSION_BUILD)
    set(REDPANDA_VERSION "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}.${APP_VERSION_BUILD}")
else()
    set(REDPANDA_VERSION "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}")
endif()

if (APP_VERSION_PRE_RELEASE)
    set(REDPANDA_VERSION "${REDPANDA_VERSION}-${APP_VERSION_PRE_RELEASE}")
endif()

if (UNIX AND NOT APPLE)
    set(XDG ON)
else()
    set(XDG OFF)
endif()

function(target_qt_plain_cpp target)
    foreach (arg IN LISTS ARGN)
        target_sources(${target} PRIVATE ${arg}.cpp ${arg}.h)
        # for Qt 5 lupdate
        list(APPEND ${target}_LUPDATE_SOURCES ${arg}.cpp)
        set(${target}_LUPDATE_SOURCES ${${target}_LUPDATE_SOURCES} PARENT_SCOPE)
    endforeach()
endfunction()

function(target_moc_classes target)
    foreach(arg IN LISTS ARGN)
        target_sources(${target} PRIVATE ${arg}.cpp ${arg}.h)
        # for Qt 5 lupdate
        list(APPEND ${target}_LUPDATE_SOURCES ${arg}.cpp)
        set(${target}_LUPDATE_SOURCES ${${target}_LUPDATE_SOURCES} PARENT_SCOPE)
    endforeach()
endfunction()

function(target_ui_classes target)
    foreach(arg IN LISTS ARGN)
        target_sources(${target} PRIVATE ${arg}.cpp ${arg}.h ${arg}.ui)
        # for Qt 5 lupdate
        list(APPEND ${target}_LUPDATE_SOURCES ${arg}.cpp ${arg}.ui)
        set(${target}_LUPDATE_SOURCES ${${target}_LUPDATE_SOURCES} PARENT_SCOPE)
    endforeach()
endfunction()

function(target_embed_translations target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" TS_FILES)

    set(qrc_dir ${CMAKE_CURRENT_BINARY_DIR}/.qrc)
    file(MAKE_DIRECTORY ${qrc_dir})

    set(qrc_file ${qrc_dir}/${target}_qmake_qmake_qm_files.qrc)

    set(qm_files)
    if (${QT_VERSION_MAJOR} GREATER_EQUAL 6)
        qt_add_translations(${target}
            SOURCE_TARGETS ${target}
            QM_FILES_OUTPUT_VARIABLE qm_files
            TS_FILES ${arg_TS_FILES})
    else()
        qt_add_translation(qm_files ${arg_TS_FILES})

        # the lupdate target
        add_custom_target(${target}_lupdate)
        add_dependencies(update_translations ${target}_lupdate)

        # source list file
        set(lupdate_dir ${CMAKE_CURRENT_BINARY_DIR}/.lupdate)
        set(lupdate_lst_file ${lupdate_dir}/${target}_lupdate.lst)
        file(WRITE ${lupdate_lst_file})
        foreach (source_file IN LISTS ${target}_LUPDATE_SOURCES)
            file(APPEND ${lupdate_lst_file} "${source_file}\n")
        endforeach()

        # lupdate commands
        foreach (ts_file IN LISTS arg_TS_FILES)
            add_custom_command(
                TARGET ${target}_lupdate POST_BUILD
                COMMAND ${Qt5_LUPDATE_EXECUTABLE}
                ARGS @${lupdate_lst_file} -ts ${ts_file}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        endforeach()
    endif()

    file(WRITE ${qrc_file} "<RCC><qresource prefix=\"/i18n\">")

    foreach(qm_file IN LISTS qm_files)
        get_filename_component(qm_filename ${qm_file} NAME)
        file(
            APPEND ${qrc_file}
                "<file alias=\"${qm_filename}\">${qm_file}</file>")
    endforeach()

    file(APPEND ${qrc_file} "</qresource></RCC>")

    target_sources(${target} PRIVATE ${qrc_file})
endfunction()

function(target_embed_resources target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" RESOURCE_NAME FILES)

    set(qrc_dir ${CMAKE_CURRENT_BINARY_DIR}/.qrc)
    file(MAKE_DIRECTORY ${qrc_dir})

    set(qrc_file ${qrc_dir}/${arg_RESOURCE_NAME}.qrc)

    file(WRITE ${qrc_file} "<RCC><qresource prefix=\"/\">")

    foreach(res_file IN LISTS arg_FILES)
        file(RELATIVE_PATH res_filename ${CMAKE_CURRENT_SOURCE_DIR} ${res_file})
        file(
            APPEND ${qrc_file}
                "<file alias=\"${res_filename}\">${res_file}</file>")
    endforeach()

    file(APPEND ${qrc_file} "</qresource></RCC>")

    target_sources(${target} PRIVATE ${qrc_file})
endfunction()

function(target_embed_qtbase_translations target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" QM_FILES)

    set(qrc_dir ${CMAKE_CURRENT_BINARY_DIR}/.qrc)
    file(MAKE_DIRECTORY ${qrc_dir})

    set(qrc_file ${qrc_dir}/qtbase_translation.qrc)

    file(WRITE ${qrc_file} "<RCC><qresource prefix=\"translations\">")

    foreach(qm_file IN LISTS arg_QM_FILES)
        get_filename_component(qm_filename ${qm_file} NAME)
        file(
            APPEND ${qrc_file}
                "<file alias=\"${qm_filename}\">${qm_file}</file>")
    endforeach()

    file(APPEND ${qrc_file} "</qresource></RCC>")

    target_sources(${target} PRIVATE ${qrc_file})
endfunction()

set(GLOBAL_COMPILE_DEFINITIONS)

if (WIN32)
    if(QT_VERSION_MAJOR GREATER_EQUAL 6)
        set(REDPANDA_WIN32_WINNT 0x0A00)
    else()
        set(REDPANDA_WIN32_WINNT 0x0601)
    endif()
    list(APPEND GLOBAL_COMPILE_DEFINITIONS
        _WIN32_WINNT=${REDPANDA_WIN32_WINNT} WIN32_LEAN_AND_MEAN)
    if (MSVC)
        list(APPEND GLOBAL_COMPILE_DEFINITIONS NOMINMAX)
        add_compile_options(/utf-8)
    endif()
endif()

if (OVERRIDE_MALLOC)
    link_libraries(${OVERRIDE_MALLOC})
endif()

add_subdirectory(RedPandaIDE)
if (LUA_ADDON)
    add_subdirectory(libs/lua)
endif()
add_subdirectory(libs/qsynedit)
add_subdirectory(libs/redpanda_qt_utils)
add_subdirectory(tools/consolepauser)

if (VCS)
    if (WIN32)
        add_subdirectory(tools/redpanda-win-git-askpass)
    else()
        add_subdirectory(tools/redpanda-git-askpass)
    endif()
endif()

#############
# Resources #
#############

# templates
if (XDG)
    install(
        DIRECTORY platform/linux/templates
        DESTINATION share/${APP_NAME}
        FILES_MATCHING PATTERN "*")
elseif(WIN32)
    if(${FILESYSTEM_LAYOUT} STREQUAL "hierarchy")
        install(
            DIRECTORY platform/windows/templates/
            DESTINATION share/${APP_NAME}/templates
            FILES_MATCHING PATTERN "*")
        install(
            DIRECTORY platform/windows/templates-win64/
            DESTINATION share/${APP_NAME}/templates
            FILES_MATCHING PATTERN "*")
    elseif(${FILESYSTEM_LAYOUT} STREQUAL "flat")
        install(
            DIRECTORY platform/windows/templates/
            DESTINATION templates
            FILES_MATCHING PATTERN "*")
        install(
            DIRECTORY platform/windows/templates-win64/
            DESTINATION templates
            FILES_MATCHING PATTERN "*")
    else()
        message(FATAL_ERROR "Unknown FILESYSTEM_LAYOUT: ${FILESYSTEM_LAYOUT}")
    endif()
endif()

# docs
# Bilingual user documentation, kept with all install layouts.
set(FORK_DOCUMENTATION_FILES
    docs/README.md
    docs/FEATURES.md docs/FUNZIONALITA.md
    docs/FORK_CHANGES.md docs/MODIFICHE_FORK.md
    docs/GCC_DIAGNOSTICS.md docs/TRADUZIONE_DIAGNOSTICI.md
    docs/WINDOWS_DISTRIBUTION.md docs/DISTRIBUZIONE_WINDOWS_FORK.md
    docs/ITALIAN_TERMINOLOGY.md docs/TERMINOLOGIA_IT.md
    docs/TESTING.md docs/TEST_IT.md)
if(${FILESYSTEM_LAYOUT} STREQUAL "hierarchy")
    install(
        FILES README.md README.it.md NEWS.md LICENSE FORK_NOTICE.txt
        DESTINATION share/doc/${APP_NAME})
    install(FILES ${FORK_DOCUMENTATION_FILES} DESTINATION share/doc/${APP_NAME}/docs)
    install(DIRECTORY docs/assets DESTINATION share/doc/${APP_NAME}/docs)
elseif(${FILESYSTEM_LAYOUT} STREQUAL "flat")
    install(
        FILES README.md README.it.md NEWS.md LICENSE FORK_NOTICE.txt
        DESTINATION .)
    install(FILES ${FORK_DOCUMENTATION_FILES} DESTINATION docs)
    install(DIRECTORY docs/assets DESTINATION docs)
elseif(${FILESYSTEM_LAYOUT} STREQUAL "bundle")
    install(
        FILES README.md README.it.md NEWS.md LICENSE FORK_NOTICE.txt
        DESTINATION RedPandaIDE.app/Contents/Resources/doc)
    install(FILES ${FORK_DOCUMENTATION_FILES} DESTINATION RedPandaIDE.app/Contents/Resources/doc/docs)
    install(DIRECTORY docs/assets DESTINATION RedPandaIDE.app/Contents/Resources/doc/docs)
else()
    message(FATAL_ERROR "Unknown FILESYSTEM_LAYOUT: ${FILESYSTEM_LAYOUT}")
endif()

# icon
if (XDG)
    install(
        FILES platform/linux/redpandaide.svg
        DESTINATION share/icons/hicolor/scalable/apps)
endif()

# Raster launcher icons are provided at native desktop sizes.
if(XDG)
    foreach(ICON_SIZE 16 24 32 48 64 128 256 512)
        install(FILES platform/linux/icons/${ICON_SIZE}x${ICON_SIZE}/apps/redpandaide.png
            DESTINATION share/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps)
    endforeach()
endif()

# desktop entry
IF(XDG)
    configure_file(
        platform/linux/RedPandaIDE.desktop.in
        RedPandaIDE.desktop)
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/RedPandaIDE.desktop
        DESTINATION share/applications)
endif()

# mime type
if(XDG)
    install(
        FILES platform/linux/redpandaide.xml
        DESTINATION share/mime/packages)
endif()

# qt.conf
if(WIN32)
    if(${FILESYSTEM_LAYOUT} STREQUAL "hierarchy")
        # skip, bindir is shared
    elseif(${FILESYSTEM_LAYOUT} STREQUAL "flat")
        install(
            FILES platform/windows/qt.conf
            DESTINATION .)
    else()
        message(FATAL_ERROR "Unknown FILESYSTEM_LAYOUT: ${FILESYSTEM_LAYOUT}")
    endif()
endif()

# devcpp.ico
if(WIN32)
    if(${FILESYSTEM_LAYOUT} STREQUAL "hierarchy")
        # TODO: where to put it?
    elseif(${FILESYSTEM_LAYOUT} STREQUAL "flat")
        install(
            FILES RedPandaIDE/images/devcpp.ico
            DESTINATION .)
    else()
        message(FATAL_ERROR "Unknown FILESYSTEM_LAYOUT: ${FILESYSTEM_LAYOUT}")
    endif()
endif()

# Development-only report validation; Python is never an IDE runtime requirement.
add_executable(rust-workflow-checks EXCLUDE_FROM_ALL
    test/rust-workflow-checks.cpp RedPandaIDE/src/rustworkspace.cpp RedPandaIDE/src/rustworkspace.h RedPandaIDE/src/rusttools.cpp RedPandaIDE/src/rustprojectoptions.cpp)
target_include_directories(rust-workflow-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-workflow-checks PRIVATE qsynedit Qt::Test)
add_executable(rust-hover-state-probe EXCLUDE_FROM_ALL test/rust-hover-state-probe.cpp)
add_executable(rust-lsp-checks EXCLUDE_FROM_ALL test/rust-lsp-checks.cpp
    RedPandaIDE/src/rustlspclient.cpp RedPandaIDE/src/rustlspclient.h
    RedPandaIDE/src/rusttools.cpp RedPandaIDE/src/rustworkspace.cpp RedPandaIDE/src/rustworkspace.h RedPandaIDE/src/rustprojectoptions.cpp)
target_include_directories(rust-lsp-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-lsp-checks PRIVATE qsynedit Qt::Test)
target_compile_definitions(rust-lsp-checks PRIVATE REDPANDA_CPP_VERSION="${REDPANDA_VERSION}")
add_executable(rust-terminal-checks EXCLUDE_FROM_ALL test/rust-terminal-checks.cpp
    RedPandaIDE/src/rustterminal.cpp RedPandaIDE/src/rustterminal.h RedPandaIDE/src/rusttools.cpp)
target_include_directories(rust-terminal-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-terminal-checks PRIVATE Qt::Test)
add_executable(rust-snippet-checks EXCLUDE_FROM_ALL test/rust-snippet-checks.cpp RedPandaIDE/src/rustsnippet.cpp)
target_include_directories(rust-snippet-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-snippet-checks PRIVATE Qt::Test)
add_executable(rust-debug-checks EXCLUDE_FROM_ALL test/rust-debug-checks.cpp
    RedPandaIDE/src/rustdebugsession.cpp RedPandaIDE/src/rustdebugsession.h
    RedPandaIDE/src/rustterminal.cpp RedPandaIDE/src/rustterminal.h RedPandaIDE/src/rusttools.cpp)
target_include_directories(rust-debug-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-debug-checks PRIVATE Qt::Test Qt::Network)
add_executable(rust-project-checks EXCLUDE_FROM_ALL test/rust-project-checks.cpp
    RedPandaIDE/src/rustprojectoptions.cpp RedPandaIDE/src/rusttools.cpp
    RedPandaIDE/src/rustworkspace.cpp RedPandaIDE/src/rustworkspace.h)
target_include_directories(rust-project-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-project-checks PRIVATE Qt::Test)
add_executable(rust-text-edit-checks EXCLUDE_FROM_ALL test/rust-text-edit-checks.cpp RedPandaIDE/src/rusttextedits.cpp test/support/rusttextedits-baseline.cpp)
target_include_directories(rust-text-edit-checks PRIVATE test/support)
target_compile_definitions(rust-text-edit-checks PRIVATE RUST_TEXT_EDITS_TESTING)
add_executable(rust-text-edit-benchmark EXCLUDE_FROM_ALL test/rust-text-edit-benchmark.cpp RedPandaIDE/src/rusttextedits.cpp test/support/rusttextedits-baseline.cpp)
target_include_directories(rust-text-edit-benchmark PRIVATE RedPandaIDE/src test/support)
target_link_libraries(rust-text-edit-benchmark PRIVATE Qt::Core)
if(WIN32)
    target_link_libraries(rust-text-edit-benchmark PRIVATE psapi)
endif()
add_executable(rust-clean-checks EXCLUDE_FROM_ALL test/rust-clean-checks.cpp RedPandaIDE/src/rustcleanplan.cpp)
add_executable(rust-outline-checks EXCLUDE_FROM_ALL test/rust-outline-checks.cpp RedPandaIDE/src/rustoutlinemodel.cpp RedPandaIDE/src/rustoutlinemodel.h)
target_include_directories(rust-outline-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-outline-checks PRIVATE Qt::Widgets Qt::Test)
add_executable(rust-shared-checks EXCLUDE_FROM_ALL test/rust-shared-checks.cpp)
target_include_directories(rust-shared-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-shared-checks PRIVATE qsynedit Qt::Widgets Qt::Test)
add_executable(rust-format-checks EXCLUDE_FROM_ALL test/rust-format-checks.cpp RedPandaIDE/src/reformatter/rustformatter.cpp RedPandaIDE/src/reformatter/basereformatter.cpp RedPandaIDE/src/reformatter/basereformatter.h)
target_include_directories(rust-format-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-format-checks PRIVATE Qt::Core Qt::Test)
target_include_directories(rust-clean-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-clean-checks PRIVATE Qt::Test Qt::Concurrent)
if(WIN32)
    target_link_libraries(RedPandaIDE PRIVATE ole32)
    target_link_libraries(rust-clean-checks PRIVATE ole32)
endif()
target_include_directories(rust-text-edit-checks PRIVATE RedPandaIDE/src)
target_link_libraries(rust-text-edit-checks PRIVATE Qt::Test Qt::Widgets qsynedit)
if(WIN32)
    add_executable(rust-console-bridge tools/rust-console-bridge.cpp)
    set_target_properties(rust-console-bridge PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(MINGW)
        target_link_options(rust-console-bridge PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
    add_dependencies(rust-terminal-checks rust-console-bridge)
    add_dependencies(rust-debug-checks rust-console-bridge)
    add_dependencies(RedPandaIDE rust-console-bridge)
    install(TARGETS rust-console-bridge RUNTIME DESTINATION .)
endif()
target_link_libraries(rust-hover-state-probe PRIVATE qsynedit)
add_executable(rust-icon-checks EXCLUDE_FROM_ALL test/rust-icon-checks.cpp RedPandaIDE/icons.qrc)
set_target_properties(rust-icon-checks PROPERTIES AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/RedPandaIDE/src/widgets")
target_include_directories(rust-icon-checks PRIVATE RedPandaIDE/src)
target_compile_definitions(rust-icon-checks PRIVATE RUST_IDE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/RedPandaIDE")
target_link_libraries(rust-icon-checks PRIVATE Qt::Widgets Qt::Test Qt::Xml)
if(BUILD_TESTING)
    foreach(RUST_CHECK workflow lsp terminal snippet debug project text-edit icon clean outline shared format)
        add_test(NAME test-rust-${RUST_CHECK} COMMAND rust-${RUST_CHECK}-checks -o ${CMAKE_BINARY_DIR}/rust-${RUST_CHECK}-ctest.txt,txt)
        set_tests_properties(test-rust-${RUST_CHECK} PROPERTIES TIMEOUT 180 LABELS "rust;functional" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_dependencies(all-test-targets rust-${RUST_CHECK}-checks)
    endforeach()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME test-stabilization-report
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/test_stabilization_report.py)
        set_tests_properties(test-stabilization-report PROPERTIES
            TIMEOUT 15 LABELS "infrastructure")
    endif()
endif()
