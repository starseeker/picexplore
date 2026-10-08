enable_testing()

# Justified layout test
add_executable(justified_layout_test justified_layout_test.cpp)
target_include_directories(justified_layout_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(justified_layout_test PRIVATE -g)
add_test(NAME justified_layout_test COMMAND justified_layout_test)

# Treemap layout test
add_executable(treemap_layout_test treemap_layout_test.cpp)
target_include_directories(treemap_layout_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(treemap_layout_test PRIVATE -g -O2)
add_test(NAME treemap_layout_test COMMAND treemap_layout_test)

# Database test
add_executable(database_test
    database_test.cpp
    ${CMAKE_SOURCE_DIR}/src/database.cpp
    ${CMAKE_SOURCE_DIR}/src/sift_feature.cpp
    ${CMAKE_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_SOURCE_DIR}/third_party/TinyEXIF.cpp
    ${VLSIFT_DIR}/vl_sift.cpp
)
target_include_directories(database_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${VLSIFT_DIR}
    ${XXHASH_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${CONCURRENTQUEUE_DIR}
    ${JPEG_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${WEBP_INCLUDE_DIRS}
    ${TIFF_INCLUDE_DIRS}
)
target_link_libraries(database_test
    xxhash
    lmdb
    ${JPEG_LIBRARIES}
    ${PNG_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${WEBP_LIBRARIES}
    ${TIFF_LIBRARIES}
)
target_compile_definitions(database_test PRIVATE
    _LARGEFILE64_SOURCE
    _FILE_OFFSET_BITS=64
    TINYEXIF_NO_XMP_SUPPORT
)
target_compile_options(database_test PRIVATE -g -O2)
add_test(NAME database_test COMMAND database_test)

# SIFT similarity test and benchmark
add_executable(sift_test
    sift_test.cpp
    ${CMAKE_SOURCE_DIR}/src/sift_feature.cpp
    ${CMAKE_SOURCE_DIR}/src/database.cpp
    ${CMAKE_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_SOURCE_DIR}/third_party/TinyEXIF.cpp
    ${VLSIFT_DIR}/vl_sift.cpp
)
target_include_directories(sift_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${VLSIFT_DIR}
    ${XXHASH_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${CONCURRENTQUEUE_DIR}
    ${JPEG_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${WEBP_INCLUDE_DIRS}
    ${TIFF_INCLUDE_DIRS}
)
target_link_libraries(sift_test
    xxhash
    lmdb
    ${JPEG_LIBRARIES}
    ${PNG_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${WEBP_LIBRARIES}
    ${TIFF_LIBRARIES}
)
target_compile_definitions(sift_test PRIVATE
    _LARGEFILE64_SOURCE
    _FILE_OFFSET_BITS=64
    TINYEXIF_NO_XMP_SUPPORT
)
target_compile_options(sift_test PRIVATE -g -O2)
add_test(NAME sift_test COMMAND sift_test)

# Settings test
add_executable(settings_test
    settings_test.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/app_settings.cpp
)
target_include_directories(settings_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(settings_test PRIVATE -g)
add_test(NAME settings_test COMMAND settings_test)

# PDF Generation and Export test
add_executable(pdf_test
    pdf_test.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/pdf.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/image_store.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/layout_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/file_type_colors.cpp
    ${CMAKE_SOURCE_DIR}/src/database.cpp
    ${CMAKE_SOURCE_DIR}/src/sift_feature.cpp
    ${CMAKE_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_SOURCE_DIR}/third_party/TinyEXIF.cpp
    ${VLSIFT_DIR}/vl_sift.cpp
)
target_include_directories(pdf_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cli
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/third_party
    ${VLSIFT_DIR}
    ${XXHASH_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${CONCURRENTQUEUE_DIR}
    ${JPEG_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${WEBP_INCLUDE_DIRS}
    ${TIFF_INCLUDE_DIRS}
)
target_link_libraries(pdf_test
    xxhash
    lmdb
    ${JPEG_LIBRARIES}
    ${PNG_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${WEBP_LIBRARIES}
    ${TIFF_LIBRARIES}
)
target_compile_definitions(pdf_test PRIVATE
    _LARGEFILE64_SOURCE
    _FILE_OFFSET_BITS=64
    TINYEXIF_NO_XMP_SUPPORT
)
target_compile_options(pdf_test PRIVATE -g -O2)
add_test(NAME pdf_test COMMAND pdf_test)

# Catalog tests exercise the same implementation and dependencies as PDF export.
get_target_property(CATALOG_TEST_SOURCES pdf_test SOURCES)
list(REMOVE_ITEM CATALOG_TEST_SOURCES pdf_test.cpp)
add_library(catalog_test_support STATIC ${CATALOG_TEST_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/gui/thumbnail_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/full_res_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/tile_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/scan_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/inotify_watcher.cpp
)
foreach(prop INCLUDE_DIRECTORIES LINK_LIBRARIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    get_target_property(value pdf_test ${prop})
    set_property(TARGET catalog_test_support PROPERTY ${prop} ${value})
endforeach()
get_target_property(catalog_dependencies pdf_test LINK_LIBRARIES)
set_property(TARGET catalog_test_support PROPERTY INTERFACE_LINK_LIBRARIES ${catalog_dependencies})
function(add_catalog_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE catalog_test_support)
    get_target_property(includes pdf_test INCLUDE_DIRECTORIES)
    target_include_directories(${name} PRIVATE ${includes})
    # These tests use assert for invariants, including in release builds.
    target_compile_options(${name} PRIVATE -UNDEBUG)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
add_catalog_test(catalog_test)

find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    add_executable(gui_session_test gui_session_test.cpp
        ${CMAKE_SOURCE_DIR}/src/gui/main_window.cpp
        ${CMAKE_SOURCE_DIR}/src/gui/virtual_viewport.cpp
        ${CMAKE_SOURCE_DIR}/src/gui/info_panel.cpp
        ${CMAKE_SOURCE_DIR}/src/gui/pdf_export_dialog.cpp
        ${CMAKE_SOURCE_DIR}/src/gui/app_settings.cpp
    )
    get_target_property(includes pdf_test INCLUDE_DIRECTORIES)
    target_include_directories(gui_session_test PRIVATE ${includes} ${FLTK_INCLUDE_DIR})
    target_link_libraries(gui_session_test PRIVATE catalog_test_support ${FLTK_LIBRARIES})
    target_compile_options(gui_session_test PRIVATE -UNDEBUG)
    add_test(NAME gui_session_test COMMAND ${XVFB_RUN} -a $<TARGET_FILE:gui_session_test>)
    set_tests_properties(gui_session_test PROPERTIES TIMEOUT 30)
endif()


# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
