# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

project(PEXP)  # Picture Explorer

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required system packages
if(WIN32)
    # Windows: Use find_library and find_path for libjpeg
    find_path(JPEG_INCLUDE_DIR jpeglib.h)
    find_library(JPEG_LIBRARY NAMES jpeg libjpeg)
    if(NOT JPEG_INCLUDE_DIR OR NOT JPEG_LIBRARY)
        message(FATAL_ERROR "libjpeg not found. Please install libjpeg-turbo or libjpeg.")
    endif()
    set(JPEG_LIBRARIES ${JPEG_LIBRARY})
    set(JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR})
else()
    # Unix-like systems: Use pkg-config or find_package
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(JPEG REQUIRED libjpeg)
    else()
        find_package(JPEG REQUIRED)
        set(JPEG_LIBRARIES ${JPEG_LIBRARIES})
        set(JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR})
    endif()
endif()

# Third-party dependencies paths
set(XXHASH_DIR ${CMAKE_SOURCE_DIR}/third_party/xxHash)
set(STB_DIR ${CMAKE_SOURCE_DIR}/third_party/stb)
set(LMDB_DIR ${CMAKE_SOURCE_DIR}/third_party/lmdb/libraries/liblmdb)
set(CXXOPTS_DIR ${CMAKE_SOURCE_DIR}/third_party/cxxopts)
set(STRUETYPE_DIR ${CMAKE_SOURCE_DIR}/third_party/struetype)
set(CONCURRENTQUEUE_DIR ${CMAKE_SOURCE_DIR}/third_party/concurrentqueue)

# xxHash - compile the source
add_library(xxhash STATIC ${XXHASH_DIR}/xxhash.c)
target_include_directories(xxhash PUBLIC ${XXHASH_DIR})

# LMDB - compile the source
add_library(lmdb STATIC ${LMDB_DIR}/mdb.c ${LMDB_DIR}/midl.c)
target_include_directories(lmdb PUBLIC ${LMDB_DIR})
target_compile_definitions(lmdb PRIVATE MDB_MAXKEYSIZE=511)

# Test programs
add_subdirectory(tests)

# New unified picscan executable
add_executable(picscan picscan.cpp database.cpp pdf.cpp utils.cpp third_party/TinyEXIF.cpp)
target_include_directories(picscan PRIVATE
    ${XXHASH_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${STRUETYPE_DIR}
    ${STRUETYPE_DIR}/examples
    ${CONCURRENTQUEUE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
    ${JPEG_INCLUDE_DIRS}
.)
target_link_libraries(picscan xxhash lmdb ${JPEG_LIBRARIES})
target_compile_options(picscan PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
