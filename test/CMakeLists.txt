# Basic test of justified_layout.hpp
add_executable(justified_layout_test justified_layout_test.cpp)
target_compile_options(justified_layout_test PRIVATE -g)
target_include_directories(justified_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# StateStore and EventBus unit tests
add_executable(state_store_test state_store_test.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp)
target_compile_options(state_store_test PRIVATE -g)
target_include_directories(state_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(state_store_test PRIVATE pthread)

# StateStore integration test (without FLTK dependencies)
add_executable(state_store_integration_test state_store_integration_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/state_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/event_bus.cpp)
target_compile_options(state_store_integration_test PRIVATE -g)
target_include_directories(state_store_integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(state_store_integration_test PRIVATE pthread)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
