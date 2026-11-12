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

