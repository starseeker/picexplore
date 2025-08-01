# Basic test of justified_layout.hpp
add_executable(justified_layout_test justified_layout_test.cpp)
target_compile_options(justified_layout_test PRIVATE -g)
target_include_directories(justified_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
