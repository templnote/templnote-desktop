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

# QML regression tests: compile the editor QML into the test executable and
# drive it with Qt Test. This keeps the app target untouched while still
# exercising the real delegates, focus handling and undo/redo routing.
set(TEST_QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qml")

set_source_files_properties(
    "${TEST_QML_DIR}/Theme.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS "qml/Theme.qml"
)

foreach(qml_file
    Editor.qml
    BlockRenderer.qml
    blocks/TextBlock.qml
    blocks/ParagraphBlock.qml
    blocks/HeadingBlock.qml
    blocks/BulletBlock.qml
    blocks/NumberedBlock.qml
)
    set_source_files_properties(
        "${TEST_QML_DIR}/${qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "qml/${qml_file}"
    )
endforeach()

qt_add_executable(tst_editor
    tst_editor.cpp
)

qt_add_qml_module(tst_editor
    URI Templnote
    VERSION 1.0

    QML_FILES
    EditorTestWindow.qml
    ${TEST_QML_DIR}/Theme.qml
    ${TEST_QML_DIR}/Editor.qml
    ${TEST_QML_DIR}/BlockRenderer.qml
    ${TEST_QML_DIR}/blocks/TextBlock.qml
    ${TEST_QML_DIR}/blocks/ParagraphBlock.qml
    ${TEST_QML_DIR}/blocks/HeadingBlock.qml
    ${TEST_QML_DIR}/blocks/BulletBlock.qml
    ${TEST_QML_DIR}/blocks/NumberedBlock.qml
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

