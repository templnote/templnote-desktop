cmake_minimum_required(VERSION 3.21)

project( Templnote
  VERSION 0.1.0
  DESCRIPTION "Local first AI-native text editor"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)

include(CTest)

find_package(Qt6 6.5 REQUIRED COMPONENTS
  Core
  Concurrent
  Gui
  Quick
  QuickControls2
  Sql
  Test)

qt_standard_project_setup(REQUIRES 6.5)

qt_policy(SET QTP0004 NEW)

set_source_files_properties(qml/Theme.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(qml/BlockSpacing.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(qml/Preferences.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

add_library(templnote_core STATIC
  src/core/Block.h
  src/core/Block.cpp
  src/core/BlockTypes.h
  src/core/Document.h
  src/core/Document.cpp

  src/model/BlockModel.cpp
  src/model/BlockModel.h

  src/storage/AppPaths.h
  src/storage/AppPaths.cpp
  src/storage/DocumentListModel.h
  src/storage/DocumentListModel.cpp

  src/search/SearchIndex.h
  src/search/SearchIndex.cpp
  src/search/SearchResultsModel.h
  src/search/SearchResultsModel.cpp
)

target_include_directories(templnote_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(templnote_core PUBLIC
  Qt6::Core
  Qt6::Concurrent
  Qt6::Sql
)

qt_add_executable(templnote
  src/main.cpp
)

qt_add_qml_module(templnote
  URI Templnote
  VERSION 1.0

  QML_FILES
  qml/Main.qml
  qml/Theme.qml
  qml/BlockSpacing.qml
  qml/Preferences.qml
  qml/HomeScreen.qml
  qml/DocumentHeader.qml
  qml/Editor.qml
  qml/BlockRenderer.qml
  qml/CommandMenu.qml
  qml/SearchBar.qml
  qml/SearchWindow.qml
  qml/ResultsList.qml
  qml/ResultPreview.qml
  qml/PromptPanel.qml
  qml/SelectionToolbar.qml
  qml/NavigationRail.qml
  qml/FileExplorer.qml
  qml/OptionsMenu.qml
  qml/SettingsWindow.qml
  qml/ShortcutsSheet.qml

  qml/settings/AppearanceSettings.qml
  qml/settings/GeneralSettings.qml
  qml/settings/InterfaceSettings.qml
  qml/settings/EditorSettings.qml
  qml/settings/PluginsSettings.qml
  qml/settings/SettingToggle.qml
  qml/settings/SettingChoice.qml

  qml/blocks/TextBlock.qml
  qml/blocks/ParagraphBlock.qml
  qml/blocks/HeadingBlock.qml
  qml/blocks/BulletBlock.qml
  qml/blocks/NumberedBlock.qml

  qml/SearchService/SearchService.qml
  qml/SearchService/LocalSearchService.qml
  qml/SearchService/HighlightService.qml

  RESOURCES
  assets/icons/ic_fluent_folder_20_regular.svg
  assets/icons/ic_fluent_settings_20_regular.svg
  assets/icons/ic_fluent_person_20_regular.svg
  assets/icons/ic_fluent_document_add_20_regular.svg
  assets/icons/ic_fluent_document_folder_20_regular.svg
  assets/icons/ic_fluent_arrow_sort_20_regular.svg
  assets/icons/ic_fluent_search_20_regular.svg
  assets/icons/ic_fluent_chat_sparkle_20_regular.svg
  assets/icons/ic_fluent_add_20_regular.svg
  assets/icons/ic_fluent_align_space_evenly_horizontal_20_regular.svg
  assets/icons/ic_fluent_chevron_double_down_20_filled.svg
  assets/icons/ic_fluent_chevron_double_up_20_filled.svg
  assets/icons/ic_fluent_panel_left_20_filled.svg
  assets/icons/ic_fluent_panel_left_contract_20_filled.svg
  assets/icons/ic_fluent_panel_left_expand_20_filled.svg
  assets/icons/ic_fluent_animal_cat_20_regular.svg
  assets/icons/ic_fluent_send_16_regular.svg
  assets/icons/ic_fluent_library_20_regular.svg
  assets/icons/ic_fluent_lightbulb_sparkle_20_regular.svg
  assets/icons/ic_fluent_delete_20_regular.svg
  assets/icons/ic_fluent_edit_20_regular.svg
  assets/icons/ic_fluent_chevron_left_20_regular.svg
  assets/icons/ic_fluent_document_20_regular.svg
  assets/icons/ic_fluent_folder_add_20_regular.svg
  assets/icons/ic_fluent_line_horizontal_3_20_regular.svg
  assets/icons/ic_fluent_globe_search_20_regular.svg
  assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg
  assets/icons/ic_fluent_mic_pulse_20_regular.svg
  assets/icons/ic_fluent_chevron_down_20_regular.svg
  assets/icons/ic_fluent_copy_20_regular.svg
  assets/icons/ic_fluent_text_bold_20_regular.svg
  assets/icons/ic_fluent_text_italic_20_regular.svg
  assets/icons/ic_fluent_text_underline_20_regular.svg
  assets/icons/ic_fluent_text_strikethrough_20_regular.svg
  assets/icons/ic_fluent_text_subscript_20_regular.svg
  assets/icons/ic_fluent_text_superscript_20_regular.svg
  assets/icons/ic_fluent_text_color_20_regular.svg
  assets/icons/ic_fluent_text_align_left_20_regular.svg
  assets/icons/ic_fluent_text_align_center_20_regular.svg
  assets/icons/ic_fluent_text_align_right_20_regular.svg
  assets/icons/ic_fluent_text_align_justify_20_regular.svg
  assets/icons/ic_fluent_home_heart_20_regular.svg
)

target_link_libraries(templnote PRIVATE
  templnote_core
  Qt6::Core
  Qt6::QuickControls2
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

