# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

project(PEXP)  # Picture Explorer

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required system packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(JPEG REQUIRED libjpeg)
pkg_check_modules(EXIF REQUIRED libexif)

# Third-party dependencies paths
set(XXHASH_DIR ${CMAKE_SOURCE_DIR}/third_party/xxHash)
set(STB_DIR ${CMAKE_SOURCE_DIR}/third_party/stb)
set(LMDB_DIR ${CMAKE_SOURCE_DIR}/third_party/lmdb/libraries/liblmdb)
set(CXXOPTS_DIR ${CMAKE_SOURCE_DIR}/third_party/cxxopts)
set(STRUETYPE_DIR ${CMAKE_SOURCE_DIR}/third_party/struetype)
set(CONCURRENTQUEUE_DIR ${CMAKE_SOURCE_DIR}/third_party/concurrentqueue)
set(FLTK_DIR ${CMAKE_SOURCE_DIR}/third_party/fltk)

# xxHash - compile the source
add_library(xxhash STATIC ${XXHASH_DIR}/xxhash.c)
target_include_directories(xxhash PUBLIC ${XXHASH_DIR})

# LMDB - compile the source
add_library(lmdb STATIC ${LMDB_DIR}/mdb.c ${LMDB_DIR}/midl.c)
target_include_directories(lmdb PUBLIC ${LMDB_DIR})
target_compile_definitions(lmdb PRIVATE MDB_MAXKEYSIZE=511)

# FLTK - build as part of this project
option(OPTION_BUILD_EXAMPLES "Build example programs" OFF)
option(FLTK_BUILD_TEST "Build test/demo programs" OFF)
option(FLTK_BUILD_FLTK_OPTIONS "Build fltk-options" OFF)
add_subdirectory(${FLTK_DIR} EXCLUDE_FROM_ALL)

# Original executable
add_executable(justified_layout_test justified_layout_test.cpp)
target_compile_options(justified_layout_test PRIVATE -g)

# New unified picscan executable
add_executable(picscan picscan.cpp database.cpp pdf.cpp utils.cpp stb_impl.cpp)
target_include_directories(picscan PRIVATE 
    ${XXHASH_DIR}
    ${STB_DIR} 
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${STRUETYPE_DIR}
    ${STRUETYPE_DIR}/examples
    ${CONCURRENTQUEUE_DIR}
.)
target_link_libraries(picscan xxhash lmdb ${JPEG_LIBRARIES} ${EXIF_LIBRARIES})
target_compile_options(picscan PRIVATE -g)

# GUI test application
add_executable(gui_test gui/gui_test.cpp gui/Fl_JustifiedLayout.cpp database.cpp utils.cpp stb_impl.cpp)
target_include_directories(gui_test PRIVATE 
    ${XXHASH_DIR}
    ${STB_DIR} 
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${STRUETYPE_DIR}
    ${STRUETYPE_DIR}/examples
    ${CONCURRENTQUEUE_DIR}
    ${FLTK_DIR}
    .
)
target_link_libraries(gui_test xxhash lmdb fltk ${JPEG_LIBRARIES} ${EXIF_LIBRARIES})
target_compile_options(gui_test PRIVATE -g)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8