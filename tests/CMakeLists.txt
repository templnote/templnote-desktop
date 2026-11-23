add_executable(benchmark_load
    benchmark_load.cpp
)

target_link_libraries(benchmark_load PRIVATE
    templnote_core
    Qt6::Test
)

add_executable(benchmark_search
    benchmark_search.cpp
)

target_link_libraries(benchmark_search PRIVATE
    templnote_core
    Qt6::Test
)

add_test(
    NAME SearchBenchmark
    COMMAND benchmark_search
)

add_executable(benchmark_engine
    benchmark_engine.cpp
)
target_link_libraries(benchmark_engine PRIVATE
    templnote_engine
    Qt6::Test
)

add_executable(tst_document
    tst_document.cpp
)

target_link_libraries(tst_document PRIVATE
    templnote_core
    Qt6::Test
)

add_test(
    NAME DocumentTests
    COMMAND tst_document
)

# QSyntaxHighlighter needs QGuiApplication for QTextDocument layout/formats.
set_tests_properties(DocumentTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(tst_blockmodel
    tst_blockmodel.cpp
)

target_link_libraries(tst_blockmodel PRIVATE
    templnote_core
    Qt6::Test
)

add_test(
    NAME BlockModelTests
    COMMAND tst_blockmodel
)

add_executable(tst_engine
    tst_engine.cpp
)

target_link_libraries(tst_engine PRIVATE
    templnote_engine
    Qt6::Test
)

add_test(
    NAME EngineTests
    COMMAND tst_engine
)

add_executable(tst_ai
    tst_ai.cpp
)

target_link_libraries(tst_ai PRIVATE
    templnote_ai
    templnote_ai_adapters
    templnote_sync
    Qt6::Test
)

add_test(
    NAME AiTests
    COMMAND tst_ai
)

add_executable(tst_security
    tst_security.cpp
)
target_link_libraries(tst_security PRIVATE
    templnote_ai
    templnote_ai_adapters
    templnote_core
    Qt6::Test
)
add_test(
    NAME SecurityTests
    COMMAND tst_security
)

add_executable(tst_crdt
    tst_crdt.cpp
)

target_link_libraries(tst_crdt PRIVATE
    templnote_engine
    Qt6::Test
)

add_test(
    NAME CrdtTests
    COMMAND tst_crdt
)

add_executable(tst_store
    tst_store.cpp
)

target_link_libraries(tst_store PRIVATE
    templnote_store
    Qt6::Test
)

add_test(
    NAME StoreTests
    COMMAND tst_store
)

add_executable(tst_storage
    tst_storage.cpp
)

target_link_libraries(tst_storage PRIVATE
    templnote_core
    Qt6::Test
)

add_test(
    NAME StorageTests
    COMMAND tst_storage
)

add_executable(tst_searchindex
    tst_searchindex.cpp
)

target_link_libraries(tst_searchindex PRIVATE
    templnote_core
    Qt6::Test
)

add_test(
    NAME SearchIndexTests
    COMMAND tst_searchindex
)

add_executable(tst_plugins
    tst_plugins.cpp
)

target_compile_definitions(tst_plugins PRIVATE
    TEMPLNOTE_TEST_BUNDLED_PLUGINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../bundled-plugins"
)

target_link_libraries(tst_plugins PRIVATE
    templnote_plugin_core
    templnote_domain
    Qt6::Test
)

add_test(
    NAME PluginTests
    COMMAND tst_plugins
)

# QML regression tests: compile the editor QML into the test executable and
# drive it with Qt Test. This keeps the app target untouched while still
# exercising the real delegates, focus handling and undo/redo routing.
set(TEST_QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qml")

set_source_files_properties(
    "${TEST_QML_DIR}/Theme.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS "qml/Theme.qml"
)

set_source_files_properties(
    "${TEST_QML_DIR}/BlockSpacing.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS "qml/BlockSpacing.qml"
)

set_source_files_properties(
    "${TEST_QML_DIR}/Preferences.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS "qml/Preferences.qml"
)

foreach(emoji_file data.json messages.json shortcodes-github.json)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/emoji/${emoji_file}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/emoji/${emoji_file}"
    )
endforeach()

set_source_files_properties(
    "${TEST_QML_DIR}/MarkdownSyntax.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS "qml/MarkdownSyntax.qml"
)

set_source_files_properties(
    "${TEST_QML_DIR}/shaders/paper.frag.qsb" PROPERTIES
    QT_RESOURCE_ALIAS "qml/shaders/paper.frag.qsb"
)
set_source_files_properties(
    "${TEST_QML_DIR}/shaders/edgefade.frag.qsb" PROPERTIES
    QT_RESOURCE_ALIAS "qml/shaders/edgefade.frag.qsb"
)

foreach(qml_file
    Editor.qml
    PaperSheet.qml
    BlockRenderer.qml
    CommandMenu.qml
    EmojiPicker.qml
    ImageSourceMenu.qml
    SelectionToolbar.qml
    SearchWindow.qml
    ModalFocusGuard.qml
    ResultsList.qml
    ResultPreview.qml
    SearchService/SearchService.qml
    SearchService/LocalSearchService.qml
    SearchService/HighlightService.qml
    blocks/TextBlock.qml
    blocks/ParagraphBlock.qml
    blocks/HeadingBlock.qml
    blocks/BulletBlock.qml
    blocks/NumberedBlock.qml
    blocks/QuoteBlock.qml
    blocks/CodeBlock.qml
    blocks/DividerBlock.qml
    blocks/CalloutBlock.qml
    blocks/TodoBlock.qml
    blocks/ImageBlock.qml
    blocks/TableBlock.qml
    blocks/ColumnsBlock.qml
)
    set_source_files_properties(
        "${TEST_QML_DIR}/${qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "qml/${qml_file}"
    )
endforeach()

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ic_fluent_re_order_dots_horizontal_20_regular.svg"
    PROPERTIES QT_RESOURCE_ALIAS
    "assets/icons/ic_fluent_re_order_dots_horizontal_20_regular.svg"
)
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg"
    PROPERTIES QT_RESOURCE_ALIAS
    "assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg"
)

# AI settings smoke test: compile and instantiate the real AISettings.qml.
set(AI_SETTINGS_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/AISettings.qml")
set(AI_SETTING_TOGGLE_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/SettingToggle.qml")
set(AI_SETTING_SWITCH_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/SettingSwitch.qml")
set(AI_SETTINGS_BUTTON_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/SettingsButton.qml")
set_source_files_properties(${AI_SETTINGS_QML} PROPERTIES
    QT_RESOURCE_ALIAS "AISettings.qml")
set_source_files_properties(${AI_SETTING_TOGGLE_QML} PROPERTIES
    QT_RESOURCE_ALIAS "SettingToggle.qml")
set_source_files_properties(${AI_SETTING_SWITCH_QML} PROPERTIES
    QT_RESOURCE_ALIAS "SettingSwitch.qml")
set_source_files_properties(${AI_SETTINGS_BUTTON_QML} PROPERTIES
    QT_RESOURCE_ALIAS "SettingsButton.qml")
set(PLUGINS_SETTINGS_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/PluginsSettings.qml")
set(DECLARATIVE_SETTINGS_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/DeclarativePluginSettings.qml")
set(SETTING_CHOICE_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/SettingChoice.qml")
set(SETTING_SLIDER_QML "${CMAKE_CURRENT_SOURCE_DIR}/../qml/settings/SettingSlider.qml")
foreach(alias_pair
        "PluginsSettings.qml|${PLUGINS_SETTINGS_QML}"
        "DeclarativePluginSettings.qml|${DECLARATIVE_SETTINGS_QML}"
        "SettingChoice.qml|${SETTING_CHOICE_QML}"
        "SettingSlider.qml|${SETTING_SLIDER_QML}")
    string(REGEX REPLACE "^[^|]+\\|" "" qml_path "${alias_pair}")
    string(REGEX REPLACE "\\|.*$" "" qml_alias "${alias_pair}")
    set_source_files_properties("${qml_path}" PROPERTIES
        QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
set_source_files_properties("${TEST_QML_DIR}/Theme.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(tst_aisettings
    tst_aisettings.cpp
)
qt_add_qml_module(tst_aisettings
    URI TemplnoteAISettingsTest
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aisettings"
    QML_FILES
        AISettingsTest.qml
        SettingToggleTest.qml
        PluginsSettingsTest.qml
        ${TEST_QML_DIR}/Theme.qml
        ${AI_SETTING_TOGGLE_QML}
        ${AI_SETTING_SWITCH_QML}
        ${AI_SETTINGS_BUTTON_QML}
        ${AI_SETTINGS_QML}
        ${PLUGINS_SETTINGS_QML}
        ${DECLARATIVE_SETTINGS_QML}
        ${SETTING_CHOICE_QML}
        ${SETTING_SLIDER_QML}
)
target_link_libraries(tst_aisettings PRIVATE
    templnote_ai
    templnote_plugin_core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Test
)
add_test(
    NAME AISettingsQmlTests
    COMMAND $<TARGET_FILE:tst_aisettings>
)
set_tests_properties(AISettingsQmlTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

qt_add_executable(tst_editor
    tst_editor.cpp
)

qt_add_qml_module(tst_editor
    URI Templnote
    VERSION 1.0

    QML_FILES
    EditorTestWindow.qml
    ${TEST_QML_DIR}/Theme.qml
    ${TEST_QML_DIR}/BlockSpacing.qml
    ${TEST_QML_DIR}/Preferences.qml
    ${TEST_QML_DIR}/MarkdownSyntax.qml
    ${TEST_QML_DIR}/Editor.qml
    ${TEST_QML_DIR}/PaperSheet.qml
    ${TEST_QML_DIR}/BlockRenderer.qml
    ${TEST_QML_DIR}/CommandMenu.qml
    ${TEST_QML_DIR}/EmojiPicker.qml
    ${TEST_QML_DIR}/ImageSourceMenu.qml
    ${TEST_QML_DIR}/SelectionToolbar.qml
    ${TEST_QML_DIR}/blocks/TextBlock.qml
    ${TEST_QML_DIR}/blocks/ParagraphBlock.qml
    ${TEST_QML_DIR}/blocks/HeadingBlock.qml
    ${TEST_QML_DIR}/blocks/BulletBlock.qml
    ${TEST_QML_DIR}/blocks/NumberedBlock.qml
    ${TEST_QML_DIR}/blocks/QuoteBlock.qml
    ${TEST_QML_DIR}/blocks/CodeBlock.qml
    ${TEST_QML_DIR}/blocks/DividerBlock.qml
    ${TEST_QML_DIR}/blocks/CalloutBlock.qml
    ${TEST_QML_DIR}/blocks/TodoBlock.qml
    ${TEST_QML_DIR}/blocks/ImageBlock.qml
    ${TEST_QML_DIR}/blocks/TableBlock.qml
    ${TEST_QML_DIR}/blocks/ColumnsBlock.qml

    RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ic_fluent_re_order_dots_horizontal_20_regular.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg
    ${TEST_QML_DIR}/shaders/paper.frag.qsb
    ${TEST_QML_DIR}/shaders/edgefade.frag.qsb
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/emoji/data.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/emoji/messages.json
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/emoji/shortcodes-github.json
)

target_link_libraries(tst_editor PRIVATE
    templnote_core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Test
)

add_test(
    NAME EditorQmlTests
    COMMAND tst_editor
)

set_tests_properties(EditorQmlTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

qt_add_executable(tst_searchwindow
    tst_searchwindow.cpp
)

set(SEARCH_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/ic_fluent_search_20_regular.svg"
)
set_source_files_properties("${SEARCH_ICON}" PROPERTIES
    QT_RESOURCE_ALIAS "assets/icons/ic_fluent_search_20_regular.svg"
)
set_source_files_properties(
    "${TEST_QML_DIR}/SearchService/localSearchWorker.mjs" PROPERTIES
    QT_RESOURCE_ALIAS "qml/SearchService/localSearchWorker.mjs"
)

qt_add_qml_module(tst_searchwindow
    URI TemplnoteSearchTest
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/searchwindow"

    QML_FILES
    SearchWindowTest.qml
    LocalSearchServiceTest.qml
    ${TEST_QML_DIR}/Theme.qml
    ${TEST_QML_DIR}/SearchWindow.qml
    ${TEST_QML_DIR}/ModalFocusGuard.qml
    ${TEST_QML_DIR}/ResultsList.qml
    ${TEST_QML_DIR}/ResultPreview.qml
    ${TEST_QML_DIR}/SearchService/SearchService.qml
    ${TEST_QML_DIR}/SearchService/LocalSearchService.qml
    ${TEST_QML_DIR}/SearchService/HighlightService.qml

    RESOURCES
    ${SEARCH_ICON}
    ${TEST_QML_DIR}/SearchService/localSearchWorker.mjs
)

target_link_libraries(tst_searchwindow PRIVATE
    templnote_core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Test
)

add_test(
    NAME SearchWindowQmlTests
    COMMAND tst_searchwindow
)

set_tests_properties(SearchWindowQmlTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

