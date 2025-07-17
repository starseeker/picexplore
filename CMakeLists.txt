# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

project(PEXP)  # Picture Explorer

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(jltest justified_layout_test.cpp)
target_compile_options(jltest PRIVATE -g)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
