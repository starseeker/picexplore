# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

project(PEXP)  # Picture Explorer

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-g)

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
        pkg_check_modules(JPEG libjpeg)
    else()
        find_package(JPEG)
        if(JPEG_FOUND)
            set(JPEG_LIBRARIES ${JPEG_LIBRARIES})
            set(JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR})
        endif()
    endif()
endif()

# Find mlpack for similarity search
find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_search_module(MLPACK mlpack)
endif()

# Also check manually
if(NOT MLPACK_FOUND)
    find_path(MLPACK_INCLUDE_DIR NAMES mlpack/mlpack.hpp)
    find_library(MLPACK_LIBRARY NAMES mlpack)

    if(MLPACK_INCLUDE_DIR AND MLPACK_LIBRARY)
        set(MLPACK_FOUND TRUE)
        set(MLPACK_LIBRARIES ${MLPACK_LIBRARY})
        set(MLPACK_INCLUDE_DIRS ${MLPACK_INCLUDE_DIR})
    endif()
endif()

# If pkg-config found it but gave partial info, use manual detection
if(MLPACK_FOUND AND NOT MLPACK_LIBRARIES)
    # Use pkg-config info
    execute_process(COMMAND pkg-config --libs mlpack
                   OUTPUT_VARIABLE MLPACK_LIBRARIES
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND pkg-config --cflags mlpack
                   OUTPUT_VARIABLE MLPACK_CFLAGS
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
    # Extract include dirs from cflags
    string(REGEX MATCHALL "-I[^ ]*" MLPACK_INCLUDE_DIRS "${MLPACK_CFLAGS}")
    string(REGEX REPLACE "-I" "" MLPACK_INCLUDE_DIRS "${MLPACK_INCLUDE_DIRS}")
endif()

# Third-party dependencies paths
set(XXHASH_DIR ${CMAKE_SOURCE_DIR}/third_party/xxHash)
set(STB_DIR ${CMAKE_SOURCE_DIR}/third_party/stb)
set(LMDB_DIR ${CMAKE_SOURCE_DIR}/third_party/lmdb/libraries/liblmdb)
set(CXXOPTS_DIR ${CMAKE_SOURCE_DIR}/third_party/cxxopts)
set(STRUETYPE_DIR ${CMAKE_SOURCE_DIR}/third_party/struetype)
set(CONCURRENTQUEUE_DIR ${CMAKE_SOURCE_DIR}/third_party/concurrentqueue)
set(FLTK_DIR ${CMAKE_SOURCE_DIR}/third_party/fltk)

# Find system packages for dependencies we need
find_package(PkgConfig)

# xxHash - try system first, then build if needed
find_path(XXHASH_INCLUDE_DIR xxhash.h)
find_library(XXHASH_LIBRARY xxhash)
if(XXHASH_INCLUDE_DIR AND XXHASH_LIBRARY)
    set(XXHASH_LIBRARIES ${XXHASH_LIBRARY})
    set(XXHASH_INCLUDE_DIRS ${XXHASH_INCLUDE_DIR})
    set(XXHASH_FOUND TRUE)
    message(STATUS "Found system xxhash: ${XXHASH_LIBRARY}")
else()
    # Try to build from source if available
    if(EXISTS ${XXHASH_DIR}/xxhash.c)
        add_library(xxhash STATIC ${XXHASH_DIR}/xxhash.c)
        target_include_directories(xxhash PUBLIC ${XXHASH_DIR})
        set(XXHASH_LIBRARIES xxhash)
        set(XXHASH_INCLUDE_DIRS ${XXHASH_DIR})
        set(XXHASH_FOUND TRUE)
    else()
        message(WARNING "xxhash not found in system or source")
        set(XXHASH_FOUND FALSE)
    endif()
endif()

# LMDB - try system first, then build if needed  
find_path(LMDB_INCLUDE_DIR lmdb.h)
find_library(LMDB_LIBRARY lmdb)
if(LMDB_INCLUDE_DIR AND LMDB_LIBRARY)
    set(LMDB_LIBRARIES ${LMDB_LIBRARY})
    set(LMDB_INCLUDE_DIRS ${LMDB_INCLUDE_DIR})
    set(LMDB_FOUND TRUE)
    message(STATUS "Found system lmdb: ${LMDB_LIBRARY}")
else()
    # Try to build from source if available
    if(EXISTS ${LMDB_DIR}/mdb.c)
        add_library(lmdb STATIC ${LMDB_DIR}/mdb.c ${LMDB_DIR}/midl.c)
        target_include_directories(lmdb PUBLIC ${LMDB_DIR})
        target_compile_definitions(lmdb PRIVATE MDB_MAXKEYSIZE=511)
        set(LMDB_LIBRARIES lmdb)
        set(LMDB_INCLUDE_DIRS ${LMDB_DIR})
        set(LMDB_FOUND TRUE)
    else()
        message(WARNING "lmdb not found in system or source")
        set(LMDB_FOUND FALSE)
    endif()
endif()

# FLTK - build as part of this project (if available)
if(EXISTS ${FLTK_DIR}/CMakeLists.txt)
    option(OPTION_BUILD_EXAMPLES "Build example programs" OFF)
    option(FLTK_BUILD_TEST "Build test/demo programs" OFF)
    option(FLTK_BUILD_FLTK_OPTIONS "Build fltk-options" OFF)
    add_subdirectory(${FLTK_DIR} EXCLUDE_FROM_ALL)
    set(FLTK_AVAILABLE TRUE)
else()
    message(WARNING "FLTK source not found in ${FLTK_DIR}, using system FLTK")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(FLTK fltk)
    endif()
    set(FLTK_AVAILABLE ${FLTK_FOUND})
endif()

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(similarity)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
