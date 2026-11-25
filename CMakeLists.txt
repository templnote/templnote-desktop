cmake_minimum_required(VERSION 3.21)

project( Templnote
  VERSION 0.3.4
  DESCRIPTION "Local first text editor"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)

include(CTest)

find_package(Qt6 6.12 REQUIRED COMPONENTS
  Core
  Concurrent
  Gui
  Quick
  QuickControls2
  Sql
  Network
  Test)

qt_standard_project_setup(REQUIRES 6.12)

qt_policy(SET QTP0004 NEW)

# Reusable plugin SDK (git submodule pinned to an SDK tag). Kept as a separate
# project so the same source can be consumed by other Qt frontends.
add_subdirectory(third_party/templnote-plugins/sdk)
add_subdirectory(third_party/templnote-editor)

set_source_files_properties(qml/Theme.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(qml/BlockSpacing.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(qml/Preferences.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

set_source_files_properties(qml/MarkdownSyntax.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

# Revisioned document store: CAS writes, patch journal, snapshots.
# QtCore + QtSql, still no GUI dependency.
add_library(templnote_store STATIC
  src/store/DocumentStore.h
  src/store/DocumentStore.cpp
)
target_include_directories(templnote_store PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(templnote_store PUBLIC
  templnote_engine
  Qt6::Core
  Qt6::Sql
)

# LAN sync for multi-user collaboration.
add_library(templnote_sync STATIC
  src/sync/SyncManager.h
  src/sync/SyncManager.cpp
  src/tracking/ScopedTimer.h
  src/tracking/ScopedTimer.cpp
)
target_include_directories(templnote_sync PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(templnote_sync PUBLIC
  templnote_engine
  Qt6::Core
  Qt6::Network
)

add_library(templnote_core STATIC
  src/core/CodeHighlighter.h
  src/core/CodeHighlighter.cpp

  src/bridge/LiveBridge.h
  src/bridge/LiveBridge.cpp

  src/model/BlockModel.cpp
  src/model/BlockModel.h

  src/emoji/EmojiDatabase.h
  src/emoji/EmojiDatabase.cpp
  src/storage/DocumentListModel.h
  src/storage/DocumentListModel.cpp

  src/search/SearchIndex.h
  src/search/SearchIndex.cpp
  src/search/SearchResultsModel.h
  src/search/SearchResultsModel.cpp

  src/tracking/MemoryTracker.h
  src/tracking/MemoryTracker.cpp
  src/tracking/ScopedTimer.h
  src/tracking/ScopedTimer.cpp

)

target_include_directories(templnote_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(templnote_core PUBLIC
  templnote_domain
  templnote_engine
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Concurrent
  Qt6::Sql
)

qt_add_executable(templnote
  src/main.cpp
)

# Headless CLI daemon — QCoreApplication only, no GUI / QML.
add_executable(templnote-headless
  src/headless/main.cpp
)
target_link_libraries(templnote-headless PRIVATE
  templnote_store
  templnote_engine
  Qt6::Core
  Qt6::Sql
)

qt_add_qml_module(templnote
  URI Templnote
  VERSION 1.0

  QML_FILES
  qml/Main.qml
  qml/Theme.qml
  qml/BlockSpacing.qml
  qml/Preferences.qml
  qml/MarkdownSyntax.qml
  qml/PaperSheet.qml
  qml/HomeScreen.qml
  qml/DocumentHeader.qml
  qml/Editor.qml
  qml/BlockRenderer.qml
  qml/CommandMenu.qml
  qml/EmojiPicker.qml
  qml/ImageSourceMenu.qml
  qml/SearchBar.qml
  qml/SearchWindow.qml
  qml/ResultsList.qml
  qml/ResultPreview.qml
  qml/SelectionToolbar.qml
  qml/NavigationRail.qml
  qml/FileExplorer.qml
  qml/OptionsMenu.qml
  qml/SettingsWindow.qml
  qml/ShortcutsSheet.qml
  qml/ModalFocusGuard.qml

  qml/settings/AppearanceSettings.qml
  qml/settings/GeneralSettings.qml
  qml/settings/InterfaceSettings.qml
  qml/settings/EditorSettings.qml
  qml/settings/DeveloperSettings.qml
  qml/settings/PluginsSettings.qml
  qml/settings/DeclarativePluginSettings.qml
  qml/settings/SettingToggle.qml
  qml/settings/SettingSwitch.qml
  qml/settings/SettingsButton.qml
  qml/settings/SettingChoice.qml
  qml/settings/SettingSlider.qml

  qml/blocks/TextBlock.qml
  qml/blocks/ParagraphBlock.qml
  qml/blocks/HeadingBlock.qml
  qml/blocks/BulletBlock.qml
  qml/blocks/NumberedBlock.qml
  qml/blocks/QuoteBlock.qml
  qml/blocks/CodeBlock.qml
  qml/blocks/DividerBlock.qml
  qml/blocks/CalloutBlock.qml
  qml/blocks/TodoBlock.qml
  qml/blocks/ImageBlock.qml
  qml/blocks/TableBlock.qml
  qml/blocks/ColumnsBlock.qml

  qml/SearchService/SearchService.qml
  qml/SearchService/LocalSearchService.qml
  qml/SearchService/HighlightService.qml

  RESOURCES
  bundled-plugins/theme-tokyo-night/manifest.json
  bundled-plugins/theme-tokyo-night/ThemeSettings.qml
  bundled-plugins/theme-gruvbox/manifest.json
  bundled-plugins/theme-gruvbox/ThemeSettings.qml
  bundled-plugins/theme-solarized-osaka/manifest.json
  bundled-plugins/theme-solarized-osaka/ThemeSettings.qml
  bundled-plugins/theme-github/manifest.json
  bundled-plugins/theme-github/ThemeSettings.qml
  bundled-plugins/paper-background/manifest.json
  bundled-plugins/paper-background/PaperBackgroundDecoration.qml
  bundled-plugins/paper-background/PaperBackgroundSettings.qml

  qml/shaders/paper.frag.qsb
  qml/shaders/edgefade.frag.qsb
  qml/SearchService/localSearchWorker.mjs
  assets/emoji/data.json
  assets/emoji/messages.json
  assets/emoji/shortcodes-github.json
  assets/emoji/LICENSE
  assets/logo.png
  assets/icons/ic_fluent_folder_20_regular.svg
  assets/icons/ic_fluent_image_20_regular.svg
  assets/icons/ic_fluent_settings_20_regular.svg
  assets/icons/ic_fluent_developer_board_20_regular.svg
  assets/icons/ic_fluent_person_20_regular.svg
  assets/icons/ic_fluent_document_add_20_regular.svg
  assets/icons/ic_fluent_document_folder_20_regular.svg
  assets/icons/ic_fluent_arrow_sort_20_regular.svg
  assets/icons/ic_fluent_search_20_regular.svg
  assets/icons/ic_fluent_add_20_regular.svg
  assets/icons/ic_fluent_align_space_evenly_horizontal_20_regular.svg
  assets/icons/ic_fluent_chevron_double_down_20_filled.svg
  assets/icons/ic_fluent_chevron_double_up_20_filled.svg
  assets/icons/ic_fluent_panel_left_20_filled.svg
  assets/icons/ic_fluent_panel_left_contract_20_filled.svg
  assets/icons/ic_fluent_panel_left_expand_20_filled.svg
  assets/icons/ic_fluent_animal_cat_20_regular.svg
  assets/icons/ic_fluent_library_20_regular.svg
  assets/icons/ic_fluent_delete_20_regular.svg
  assets/icons/ic_fluent_edit_20_regular.svg
  assets/icons/ic_fluent_chevron_left_20_regular.svg
  assets/icons/ic_fluent_document_20_regular.svg
  assets/icons/ic_fluent_folder_add_20_regular.svg
  assets/icons/ic_fluent_line_horizontal_3_20_regular.svg
  assets/icons/ic_fluent_arrow_autofit_width_20_regular.svg
  assets/icons/ic_fluent_globe_search_20_regular.svg
  assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg
  assets/icons/ic_fluent_re_order_dots_horizontal_20_regular.svg
  assets/icons/ic_fluent_chevron_down_20_regular.svg
  assets/icons/ic_fluent_copy_20_regular.svg
  assets/icons/ic_fluent_text_bold_20_regular.svg
  assets/icons/ic_fluent_text_italic_20_regular.svg
  assets/icons/ic_fluent_text_underline_20_regular.svg
  assets/icons/ic_fluent_text_strikethrough_20_regular.svg
  assets/icons/ic_fluent_text_subscript_20_regular.svg
  assets/icons/ic_fluent_text_superscript_20_regular.svg
  assets/icons/ic_fluent_text_color_20_regular.svg
  assets/icons/ic_fluent_align_left_20_regular.svg
  assets/icons/ic_fluent_align_center_vertical_20_regular.svg
  assets/icons/ic_fluent_align_right_20_regular.svg
  assets/icons/ic_fluent_home_heart_20_regular.svg
  assets/icons/ic_fluent_checkbox_checked_20_regular.svg
  assets/icons/ic_fluent_checkbox_checked_20_filled.svg
  assets/icons/ic_fluent_checkbox_unchecked_20_regular.svg
  assets/icons/ic_fluent_text_quote_20_regular.svg
  assets/icons/ic_fluent_line_horizontal_1_20_regular.svg
)

target_link_libraries(templnote PRIVATE
  templnote_core
  templnote_plugin_qml
  Qt6::Core
  Qt6::QuickControls2
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

