# Basic test of justified_layout.hpp
add_executable(justified_layout_test justified_layout_test.cpp)
target_compile_options(justified_layout_test PRIVATE -g)
target_include_directories(justified_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Layout debug test
if(JPEG_FOUND)
    add_executable(layout_debug_test layout_debug_test.cpp ../src/Fl_JustifiedLayout.cpp ../src/database.cpp ../src/pdf.cpp ../src/utils.cpp ../src/stb_impl.cpp ../third_party/TinyEXIF.cpp)
    target_include_directories(layout_debug_test PRIVATE
        ${XXHASH_DIR}
        ${STB_DIR}
        ${LMDB_DIR}
        ${CXXOPTS_DIR}/include
        ${STRUETYPE_DIR}
        ${STRUETYPE_DIR}/examples
        ${CONCURRENTQUEUE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
        ${JPEG_INCLUDE_DIRS}
        ${FLTK_DIR}
        .
    )
    target_link_libraries(layout_debug_test xxhash lmdb fltk ${JPEG_LIBRARIES})
    target_include_directories(layout_debug_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_compile_options(layout_debug_test PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# Incremental layout test (no GUI required)
if(JPEG_FOUND)
    add_executable(test_incremental_layout test_incremental_layout.cpp ../src/database.cpp ../src/utils.cpp ../src/stb_impl.cpp ../third_party/TinyEXIF.cpp)
    target_include_directories(test_incremental_layout PRIVATE
        ${XXHASH_DIR}
        ${STB_DIR}
        ${LMDB_DIR}
        ${STRUETYPE_DIR}
        ${STRUETYPE_DIR}/examples
        ${CONCURRENTQUEUE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
        ${JPEG_INCLUDE_DIRS}
        .
    )
    target_link_libraries(test_incremental_layout xxhash lmdb ${JPEG_LIBRARIES})
    target_include_directories(test_incremental_layout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_compile_options(test_incremental_layout PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# Incremental scan test (no GUI required)
if(JPEG_FOUND)
    add_executable(test_incremental_scan test_incremental_scan.cpp ../src/database.cpp ../src/utils.cpp ../src/stb_impl.cpp ../third_party/TinyEXIF.cpp)
    target_include_directories(test_incremental_scan PRIVATE
        ${XXHASH_DIR}
        ${STB_DIR}
        ${LMDB_DIR}
        ${STRUETYPE_DIR}
        ${STRUETYPE_DIR}/examples
        ${CONCURRENTQUEUE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
        ${JPEG_INCLUDE_DIRS}
        .
    )
    target_link_libraries(test_incremental_scan xxhash lmdb ${JPEG_LIBRARIES})
    target_include_directories(test_incremental_scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_compile_options(test_incremental_scan PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# Build the thread manager test
if(JPEG_FOUND)
    add_executable(test_thread_manager test_thread_manager.cpp ../src/thread_manager.cpp ../src/database.cpp ../src/utils.cpp ../src/stb_impl.cpp ../third_party/TinyEXIF.cpp)
    target_include_directories(test_thread_manager PRIVATE
	${XXHASH_DIR}
	${STB_DIR}
	${LMDB_DIR}
	${STRUETYPE_DIR}
	${STRUETYPE_DIR}/examples
	${CONCURRENTQUEUE_DIR}
	${CMAKE_SOURCE_DIR}/third_party
	${JPEG_INCLUDE_DIRS}
	${FLTK_DIR}
	../src
    )
    target_link_libraries(test_thread_manager xxhash lmdb fltk ${JPEG_LIBRARIES})
    target_compile_options(test_thread_manager PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
