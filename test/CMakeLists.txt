# Basic test of justified_layout.hpp
add_executable(justified_layout_test justified_layout_test.cpp)
target_compile_options(justified_layout_test PRIVATE -g)
target_include_directories(justified_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# StateStore and EventBus unit tests
add_executable(state_store_test state_store_test.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/cache_provider.cpp)
target_compile_options(state_store_test PRIVATE -g)
target_include_directories(state_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(state_store_test PRIVATE pthread)

# CacheProvider unit tests
add_executable(cache_provider_test cache_provider_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/cache_provider.cpp)
target_compile_options(cache_provider_test PRIVATE -g)
target_include_directories(cache_provider_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(cache_provider_test PRIVATE pthread)

# StateStore integration test (without FLTK dependencies)
add_executable(state_store_integration_test state_store_integration_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/cache_provider.cpp)
target_compile_options(state_store_integration_test PRIVATE -g)
target_include_directories(state_store_integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(state_store_integration_test PRIVATE pthread)

# Database DAL unit tests
if(XXHASH_FOUND AND LMDB_FOUND)
    add_executable(database_dal_test database_dal_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/database_dal_lmdb.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging.cpp)
    target_compile_options(database_dal_test PRIVATE -g)
    target_include_directories(database_dal_test PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${XXHASH_INCLUDE_DIRS}
        ${LMDB_INCLUDE_DIRS}
        ${STB_DIR}
        ${CMAKE_SOURCE_DIR}/third_party)
    target_link_libraries(database_dal_test PRIVATE ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} ${JPEG_LIBRARIES})
    
    # DatabaseManager DAL integration test
    add_executable(database_manager_dal_integration_test database_manager_dal_integration_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/database.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/database_dal_lmdb.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/TinyEXIF.cpp)
    target_compile_options(database_manager_dal_integration_test PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
    target_include_directories(database_manager_dal_integration_test PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${XXHASH_INCLUDE_DIRS}
        ${LMDB_INCLUDE_DIRS}
        ${STB_DIR}
        ${CONCURRENTQUEUE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party)
    target_link_libraries(database_manager_dal_integration_test PRIVATE ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} ${JPEG_LIBRARIES})
    
    # DAL EventBus integration test
    add_executable(database_dal_eventbus_integration_test database_dal_eventbus_integration_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/database_dal_lmdb.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/stb_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging.cpp)
    target_compile_options(database_dal_eventbus_integration_test PRIVATE -g)
    target_include_directories(database_dal_eventbus_integration_test PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${XXHASH_INCLUDE_DIRS}
        ${LMDB_INCLUDE_DIRS}
        ${STB_DIR}
        ${CMAKE_SOURCE_DIR}/third_party)
    target_link_libraries(database_dal_eventbus_integration_test PRIVATE ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} ${JPEG_LIBRARIES} pthread)

    # MVC Core Components test (without GUI dependencies)
    add_executable(mvc_core_test mvc_core_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/state_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/cache_provider.cpp)
    target_compile_options(mvc_core_test PRIVATE -g)
    target_include_directories(mvc_core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(mvc_core_test PRIVATE pthread)

    # MVC Components test (with full dependencies)
    if(FLTK_AVAILABLE)
        add_executable(mvc_components_test mvc_components_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/controllers.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/state_store.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/cache_provider.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/thread_manager.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/task_scheduler.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/logging.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/database.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/database_dal_lmdb.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/stb_impl.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/TinyEXIF.cpp)
        target_compile_options(mvc_components_test PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
        target_include_directories(mvc_components_test PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${XXHASH_INCLUDE_DIRS}
            ${LMDB_INCLUDE_DIRS}
            ${STB_DIR}
            ${CONCURRENTQUEUE_DIR}
            ${CMAKE_SOURCE_DIR}/third_party
            ${FLTK_DIR})
        
        if(FLTK_LIBRARIES)
            target_link_libraries(mvc_components_test PRIVATE ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} ${FLTK_LIBRARIES} ${JPEG_LIBRARIES} pthread)
        else()
            target_link_libraries(mvc_components_test PRIVATE ${XXHASH_LIBRARIES} ${LMDB_LIBRARIES} fltk ${JPEG_LIBRARIES} pthread)
        endif()
    endif()
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
