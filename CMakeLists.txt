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
  Gui
  QuickControls2
  Test)

qt_standard_project_setup(REQUIRES 6.5)

qt_policy(SET QTP0004 NEW)

add_library(templnote_core STATIC
  src/core/Block.h
  src/core/BlockTypes.h
  src/core/Document.h
  src/core/Document.cpp

  src/model/BlockModel.cpp
  src/model/BlockModel.h
)

target_include_directories(templnote_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(templnote_core PUBLIC
  Qt6::Core
)

qt_add_executable(templnote
  src/main.cpp
)

qt_add_qml_module(templnote
  URI Templnote
  VERSION 1.0

  QML_FILES
  qml/Main.qml
  qml/Editor.qml
  qml/BlockRenderer.qml
  qml/PromptPanel.qml
  qml/NavigationRail.qml
  qml/FileExplorer.qml

  qml/blocks/TextBlock.qml
  qml/blocks/ParagraphBlock.qml
  qml/blocks/HeadingBlock.qml
  qml/blocks/BulletBlock.qml

  RESOURCES
  assets/icons/ic_fluent_folder_20_regular.svg
  assets/icons/ic_fluent_settings_20_regular.svg
  assets/icons/ic_fluent_person_20_regular.svg
  assets/icons/ic_fluent_document_add_20_regular.svg
  assets/icons/ic_fluent_add_20_regular.svg
  assets/icons/ic_fluent_align_space_evenly_horizontal_20_regular.svg
  assets/icons/ic_fluent_chevron_double_down_20_filled.svg
  assets/icons/ic_fluent_chevron_double_up_20_filled.svg
  assets/icons/ic_fluent_panel_left_20_filled.svg
  assets/icons/ic_fluent_panel_left_contract_20_filled.svg
  assets/icons/ic_fluent_panel_left_expand_20_filled.svg
  assets/icons/ic_fluent_animal_cat_20_regular.svg
  assets/icons/ic_fluent_send_20_color.svg
)

target_link_libraries(templnote PRIVATE
  templnote_core
  Qt6::Core
  Qt6::QuickControls2
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

