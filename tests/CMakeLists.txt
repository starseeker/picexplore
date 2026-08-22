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
    ${CMAKE_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_SOURCE_DIR}/third_party/TinyEXIF.cpp
)
target_include_directories(database_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
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

# Settings test
add_executable(settings_test
    settings_test.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/app_settings.cpp
)
target_include_directories(settings_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(settings_test PRIVATE -g)
add_test(NAME settings_test COMMAND settings_test)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
