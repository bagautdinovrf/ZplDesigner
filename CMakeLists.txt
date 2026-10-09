cmake_minimum_required(VERSION 3.25)

set(DESIGNER_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DESIGNER_VERSION_FILE}")
file(READ "${DESIGNER_VERSION_FILE}" DESIGNER_VERSION)
string(STRIP "${DESIGNER_VERSION}" DESIGNER_VERSION)
if(NOT DESIGNER_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must contain a major.minor.patch version without leading zeroes.")
endif()
foreach(part IN ITEMS "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}")
    if(part GREATER 65535)
        message(FATAL_ERROR "Each VERSION component must fit in a Windows version field (0..65535).")
    endif()
endforeach()
project(QtZplDesigner VERSION "${DESIGNER_VERSION}" LANGUAGES CXX)

set(DESIGNER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(cmake/app_version.hpp.in "${DESIGNER_GENERATED_DIR}/app_version.hpp" @ONLY)
# Keep the text marker outside include directories: on Windows VERSION would
# otherwise shadow the standard C++ <version> header.
set(DESIGNER_VERSION_MARKER "${CMAKE_CURRENT_BINARY_DIR}/metadata/VERSION")
file(GENERATE OUTPUT "${DESIGNER_VERSION_MARKER}" CONTENT "${PROJECT_VERSION}\n")

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# CMake 3.30 only describes MSVC's latest dialect as CXX23. The explicit
# /std:c++latest option below enables all language features in the installed kit.
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()

option(DESIGNER_BUILD_TESTS "Build document and interaction tests" ON)
set(QTZPL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qt-zpl" CACHE PATH
    "QtZpl source checkout; leave empty to use an installed package")

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Network)

if(QTZPL_SOURCE_DIR)
    if(NOT EXISTS "${QTZPL_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "QtZpl checkout is missing: ${QTZPL_SOURCE_DIR}. Set QTZPL_SOURCE_DIR or leave it empty to use find_package(QtZpl).")
    endif()
    set(QTZPL_BUILD_SHARED OFF CACHE BOOL "Build the bundled QtZpl as a DLL" FORCE)
    set(QTZPL_BUILD_TESTS OFF CACHE BOOL "Build QtZpl dependency tests" FORCE)
    set(QTZPL_BUILD_EXAMPLES OFF CACHE BOOL "Build QtZpl dependency examples" FORCE)
    message(STATUS "QtZpl source directory: ${QTZPL_SOURCE_DIR}")
    add_subdirectory("${QTZPL_SOURCE_DIR}" "qtzpl" EXCLUDE_FROM_ALL)
else()
    find_package(QtZpl CONFIG REQUIRED)
endif()

function(designer_target_settings target)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/std:c++latest;/utf-8;/W4;/permissive->")
    else()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
    endif()
endfunction()

add_library(designer_core STATIC
    src/core/document_operations.cpp
    src/core/document_operations.hpp
    src/core/document_revision.hpp
    src/core/project_codec.cpp
    src/core/project_codec.hpp
    src/core/element_clipboard.cpp
    src/core/element_clipboard.hpp
    src/core/editor_metadata.cpp
    src/core/editor_metadata.hpp
    src/core/eac_mark.cpp
    src/core/eac_mark.hpp
    src/core/image_operations.cpp
    src/core/image_operations.hpp
    src/core/preview_pages.hpp
    src/core/design_document.cpp
    src/core/design_document.hpp
    src/core/canvas_layout.hpp
    src/core/design_internal.hpp
    src/core/gs1_data.cpp
    src/core/gs1_data.hpp
    src/core/value_placeholders.cpp
    src/core/value_placeholders.hpp
    src/core/data_matrix_values.cpp
    src/core/data_matrix_values.hpp
    src/core/document_validation.cpp
    src/core/zpl_codec.cpp
    src/core/zpl_source_map.cpp
    src/core/zpl_source_map.hpp
    src/core/image_converter.cpp
    src/core/preview_renderer.cpp
    src/core/preset_catalog.cpp
    src/core/preset_catalog.hpp
)
target_include_directories(designer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(designer_core PUBLIC Qt6::Core Qt6::Gui QtZpl::QtZpl)
designer_target_settings(designer_core)

qt_add_resources(designer_core designer_samples
    PREFIX "/designer/samples"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/baltiysk-emblem.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/maritime-anchor.png"
)

add_library(designer_session STATIC
    src/session/document_session.cpp
    src/session/document_session.hpp
)
target_include_directories(designer_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/session")
target_link_libraries(designer_session PUBLIC designer_core Qt6::Gui)
designer_target_settings(designer_session)

add_library(designer_printing STATIC
    src/printing/printer_profiles.cpp
    src/printing/printer_profiles.hpp
    src/printing/label_printing.cpp
    src/printing/label_printing.hpp
)
target_include_directories(designer_printing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/printing")
target_link_libraries(designer_printing PUBLIC designer_core Qt6::PrintSupport Qt6::Network)
if(WIN32)
    target_link_libraries(designer_printing PRIVATE winspool)
endif()
designer_target_settings(designer_printing)

add_library(designer_widgets STATIC
    src/widgets/properties_panel.hpp
    src/widgets/properties_panel.cpp
    src/widgets/workspace_widgets.hpp
    src/widgets/workspace_widgets.cpp

    src/widgets/source_editor_adapter.hpp
    src/widgets/source_editor_adapter.cpp
    src/widgets/layers_list.hpp
    src/widgets/layers_list.cpp
    src/widgets/diagnostics_list.hpp
    src/widgets/diagnostics_list.cpp
    src/widgets/element_presentation.hpp
    src/widgets/element_presentation.cpp

    src/widgets/preview_coordinator.cpp
    src/widgets/preview_coordinator.hpp
    src/widgets/element_selection.cpp
    src/widgets/element_selection.hpp
    src/widgets/designer_theme.cpp
    src/widgets/designer_theme.hpp
    src/widgets/ui_translations.cpp
    src/widgets/ui_translations.hpp
    src/widgets/workspace_preferences.cpp
    src/widgets/workspace_preferences.hpp
    src/widgets/dot_spin_box.cpp
    src/widgets/dot_spin_box.hpp
    src/widgets/table_dialog.cpp
    src/widgets/table_dialog.hpp
    src/widgets/qr_payload_dialog.cpp
    src/widgets/qr_payload_dialog.hpp
    src/widgets/barcode_options_dialog.cpp
    src/widgets/barcode_options_dialog.hpp
    src/widgets/advanced_text_dialog.cpp
    src/widgets/advanced_text_dialog.hpp
    src/widgets/label_settings_dialog.cpp
    src/widgets/label_settings_dialog.hpp
    src/widgets/zpl_source_editor.cpp
    src/widgets/zpl_source_editor.hpp
    src/widgets/selection_properties.cpp
    src/widgets/selection_properties.hpp
    src/widgets/image_edit_dialog.cpp
    src/widgets/image_edit_dialog.hpp
    src/widgets/main_window.cpp
    src/widgets/main_window.hpp
    src/widgets/canvas_layout_dialog.cpp
    src/widgets/canvas_layout_dialog.hpp
    src/widgets/gs1_dialog.cpp
    src/widgets/gs1_dialog.hpp
    src/widgets/placeholder_values_dialog.cpp
    src/widgets/placeholder_values_dialog.hpp
    src/widgets/label_canvas.cpp
    src/widgets/label_canvas.hpp
    src/widgets/preview_widget.cpp
    src/widgets/preview_widget.hpp
    src/widgets/print_dialog.cpp
    src/widgets/print_dialog.hpp
)
target_include_directories(designer_widgets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/widgets")
target_link_libraries(designer_widgets PUBLIC designer_session designer_printing Qt6::Widgets)
designer_target_settings(designer_widgets)

# Keep standard Qt dialogs in Russian even when deployed without a separate
# translations directory. Resolve against the selected Qt kit, not a host kit.
get_filename_component(DESIGNER_QT_TRANSLATIONS_DIR "${QT6_INSTALL_TRANSLATIONS}" ABSOLUTE
    BASE_DIR "${QT6_INSTALL_PREFIX}")
set(DESIGNER_QT_RUSSIAN_TRANSLATION "${DESIGNER_QT_TRANSLATIONS_DIR}/qtbase_ru.qm")
if(NOT EXISTS "${DESIGNER_QT_RUSSIAN_TRANSLATION}")
    message(FATAL_ERROR "Install Russian Qt translations for the selected Qt kit; missing ${DESIGNER_QT_RUSSIAN_TRANSLATION}")
endif()
qt_add_resources(designer_widgets designer_translations
    PREFIX "/designer/translations"
    BASE "${DESIGNER_QT_TRANSLATIONS_DIR}"
    FILES "${DESIGNER_QT_RUSSIAN_TRANSLATION}"
)

qt_add_executable(QtZplDesigner WIN32 src/main.cpp)
target_link_libraries(QtZplDesigner PRIVATE designer_widgets)
target_include_directories(QtZplDesigner PRIVATE "${DESIGNER_GENERATED_DIR}")
if(WIN32)
    enable_language(RC)
    configure_file(cmake/app_version.rc.in "${DESIGNER_GENERATED_DIR}/app_version.rc.in" @ONLY)
    file(GENERATE OUTPUT "${DESIGNER_GENERATED_DIR}/$<CONFIG>/app_version.rc"
        INPUT "${DESIGNER_GENERATED_DIR}/app_version.rc.in")
    target_sources(QtZplDesigner PRIVATE "${DESIGNER_GENERATED_DIR}/$<CONFIG>/app_version.rc")
endif()
# Publish this marker only after the executable was successfully linked. Deploy
# and install use it, so reconfiguring cannot relabel an older executable.
add_custom_command(TARGET QtZplDesigner POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${DESIGNER_VERSION_MARKER}" "$<TARGET_FILE_DIR:QtZplDesigner>/VERSION"
    VERBATIM)
designer_target_settings(QtZplDesigner)

if(DESIGNER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
    foreach(feature document_operations source_state session_operations)
        qt_add_executable(designer_${feature}_tests tests/test_${feature}.cpp)
        if(feature STREQUAL "document_operations")
            target_link_libraries(designer_${feature}_tests PRIVATE Qt6::Test designer_core)
        else()
            target_link_libraries(designer_${feature}_tests PRIVATE Qt6::Test designer_session)
        endif()
        designer_target_settings(designer_${feature}_tests)
        add_test(NAME designer_${feature} COMMAND designer_${feature}_tests -o ${CMAKE_CURRENT_BINARY_DIR}/${feature}-results.txt,txt)
        set_tests_properties(designer_${feature} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    endforeach()
    foreach(feature eac editor_panels source_editor_adapter preview_coordinator element_selection workspace window_geometry ui_translations split_editor projects table_barcodes designer_extensions printer_profiles extended_zpl canvas_transforms advanced_dialogs)
        qt_add_executable(designer_${feature}_tests tests/test_${feature}.cpp)
        target_link_libraries(designer_${feature}_tests PRIVATE Qt6::Test designer_widgets)
        designer_target_settings(designer_${feature}_tests)
        add_test(NAME designer_${feature} COMMAND designer_${feature}_tests -o ${CMAKE_CURRENT_BINARY_DIR}/${feature}-results.txt,txt)
        set_tests_properties(designer_${feature} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    endforeach()
    foreach(feature editing_features source_tools image_operations)
        qt_add_executable(designer_${feature}_tests tests/test_${feature}.cpp)
        target_link_libraries(designer_${feature}_tests PRIVATE Qt6::Test designer_widgets)
        designer_target_settings(designer_${feature}_tests)
        add_test(NAME designer_${feature} COMMAND designer_${feature}_tests -o ${CMAKE_CURRENT_BINARY_DIR}/${feature}-results.txt,txt)
        set_tests_properties(designer_${feature} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    endforeach()
    qt_add_executable(designer_tests
        tests/test_document.cpp
    )
    target_link_libraries(designer_tests PRIVATE Qt6::Test designer_core)
    designer_target_settings(designer_tests)
    add_test(NAME designer_document COMMAND designer_tests -o ${CMAKE_CURRENT_BINARY_DIR}/document-results.txt,txt)
    set_tests_properties(designer_document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(designer_source_map_tests tests/test_source_map.cpp)
    target_link_libraries(designer_source_map_tests PRIVATE Qt6::Test designer_core)
    designer_target_settings(designer_source_map_tests)
    add_test(NAME designer_source_map COMMAND designer_source_map_tests -o ${CMAKE_CURRENT_BINARY_DIR}/source-map-results.txt,txt)
    set_tests_properties(designer_source_map PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(designer_datamatrix_template_tests tests/test_datamatrix_templates.cpp)
    target_link_libraries(designer_datamatrix_template_tests PRIVATE Qt6::Test designer_core)
    designer_target_settings(designer_datamatrix_template_tests)
    add_test(NAME designer_datamatrix_templates COMMAND designer_datamatrix_template_tests -o ${CMAKE_CURRENT_BINARY_DIR}/datamatrix-results.txt,txt)
    set_tests_properties(designer_datamatrix_templates PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(designer_session_tests tests/test_session.cpp)
    target_link_libraries(designer_session_tests PRIVATE Qt6::Test designer_session)
    designer_target_settings(designer_session_tests)
    add_test(NAME designer_session COMMAND designer_session_tests -o ${CMAKE_CURRENT_BINARY_DIR}/session-results.txt,txt)
    set_tests_properties(designer_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(designer_import_tests tests/test_milk_import.cpp)
    target_link_libraries(designer_import_tests PRIVATE Qt6::Test designer_session)
    designer_target_settings(designer_import_tests)
    add_test(NAME designer_import COMMAND designer_import_tests -o ${CMAKE_CURRENT_BINARY_DIR}/import-results.txt,txt)
    set_tests_properties(designer_import PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

    qt_add_executable(designer_printing_tests tests/test_printing.cpp)
    target_link_libraries(designer_printing_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_printing_tests)
    add_test(NAME designer_printing COMMAND designer_printing_tests -o ${CMAKE_CURRENT_BINARY_DIR}/printing-results.txt,txt)
    set_tests_properties(designer_printing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

    qt_add_executable(designer_canvas_tests tests/test_canvas.cpp)
    target_link_libraries(designer_canvas_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_canvas_tests)
    add_test(NAME designer_canvas COMMAND designer_canvas_tests -o ${CMAKE_CURRENT_BINARY_DIR}/canvas-results.txt,txt)
    set_tests_properties(designer_canvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(designer_window_tests tests/test_window.cpp)
    target_link_libraries(designer_window_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_window_tests)
    add_test(NAME designer_window COMMAND designer_window_tests -o ${CMAKE_CURRENT_BINARY_DIR}/window-results.txt,txt)
    # These UI suites repeatedly render the detailed startup label in Debug.
    set_tests_properties(designer_window PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 90
    )

    qt_add_executable(designer_gs1_toggle_tests tests/test_gs1_toggle.cpp)
    target_link_libraries(designer_gs1_toggle_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_gs1_toggle_tests)
    add_test(NAME designer_gs1_toggle COMMAND designer_gs1_toggle_tests -o ${CMAKE_CURRENT_BINARY_DIR}/gs1-toggle-results.txt,txt)
    set_tests_properties(designer_gs1_toggle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

    qt_add_executable(designer_gs1_dialog_tests tests/test_gs1_dialog.cpp)
    target_link_libraries(designer_gs1_dialog_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_gs1_dialog_tests)
    add_test(NAME designer_gs1_dialog COMMAND designer_gs1_dialog_tests -o ${CMAKE_CURRENT_BINARY_DIR}/gs1-dialog-results.txt,txt)
    set_tests_properties(designer_gs1_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(designer_preview_tests tests/test_preview.cpp)
    target_link_libraries(designer_preview_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_preview_tests)
    add_test(NAME designer_preview COMMAND designer_preview_tests -o ${CMAKE_CURRENT_BINARY_DIR}/preview-results.txt,txt)
    set_tests_properties(designer_preview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )

    qt_add_executable(designer_command_panels_tests tests/test_command_panels.cpp)
    target_link_libraries(designer_command_panels_tests PRIVATE Qt6::Test designer_widgets)
    designer_target_settings(designer_command_panels_tests)
    add_test(NAME designer_command_panels COMMAND designer_command_panels_tests -o ${CMAKE_CURRENT_BINARY_DIR}/command-panels-results.txt,txt)
    set_tests_properties(designer_command_panels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    # Explicit local-corpus inspection tool; it is not an automatic test and
    # does not embed or copy the user's production preset catalogue.
    qt_add_executable(designer_inspect_presets tests/inspect_presets.cpp)
    target_link_libraries(designer_inspect_presets PRIVATE designer_core)
    designer_target_settings(designer_inspect_presets)

    qt_add_executable(designer_inspect_zpl tests/inspect_zpl.cpp)
    target_link_libraries(designer_inspect_zpl PRIVATE designer_core)
    designer_target_settings(designer_inspect_zpl)

    add_test(NAME designer_smoke COMMAND QtZplDesigner --smoke-test)
    set_tests_properties(designer_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )
endif()

install(TARGETS QtZplDesigner RUNTIME DESTINATION bin)
install(FILES README.md CHANGELOG.md "$<TARGET_FILE_DIR:QtZplDesigner>/VERSION" DESTINATION bin)
install(DIRECTORY examples/ DESTINATION share/QtZplDesigner/examples)
