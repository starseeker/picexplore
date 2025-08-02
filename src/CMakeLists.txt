# picexplore executable
if(JPEG_FOUND AND FLTK_AVAILABLE AND XXHASH_FOUND AND LMDB_FOUND)
    add_executable(picexplore picexplore.cpp Fl_JustifiedLayout.cpp database.cpp pdf.cpp utils.cpp stb_impl.cpp thread_manager.cpp task_scheduler.cpp logging.cpp state_store.cpp event_bus.cpp cache_provider.cpp ../third_party/TinyEXIF.cpp)
    target_include_directories(picexplore PRIVATE
        ${XXHASH_INCLUDE_DIRS}
        ${STB_DIR}
        ${LMDB_INCLUDE_DIRS}
        ${CXXOPTS_DIR}/include
        ${STRUETYPE_DIR}
        ${STRUETYPE_DIR}/examples
        ${CONCURRENTQUEUE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
        ${JPEG_INCLUDE_DIRS}
        ${FLTK_DIR}
    .)
    
    if(FLTK_LIBRARIES)
        target_link_libraries(picexplore ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} ${FLTK_LIBRARIES} ${JPEG_LIBRARIES})
    else()
        target_link_libraries(picexplore ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} fltk ${JPEG_LIBRARIES})
    endif()
    
    target_compile_options(picexplore PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
else()
    if(NOT JPEG_FOUND)
        message(WARNING "libjpeg not found, skipping picexplore executable")
    endif()
    if(NOT FLTK_AVAILABLE)
        message(WARNING "FLTK not found, skipping picexplore executable")
    endif()
    if(NOT XXHASH_FOUND)
        message(WARNING "xxhash not found, skipping picexplore executable")
    endif()
    if(NOT LMDB_FOUND)
        message(WARNING "lmdb not found, skipping picexplore executable")
    endif()
endif()

# TaskScheduler test (minimal dependencies)
add_executable(task_scheduler_test task_scheduler.cpp logging.cpp ../test/task_scheduler_test.cpp)
target_include_directories(task_scheduler_test PRIVATE .)
target_compile_options(task_scheduler_test PRIVATE -g)

# Integration test (TaskScheduler + queue communication)
add_executable(integration_test task_scheduler.cpp logging.cpp ../test/integration_test.cpp)
target_include_directories(integration_test PRIVATE .)
target_compile_options(integration_test PRIVATE -g)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
