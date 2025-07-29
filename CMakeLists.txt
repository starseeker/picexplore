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

# Layout debug test
if(JPEG_FOUND)
    add_executable(layout_debug_test layout_debug_test.cpp Fl_JustifiedLayout.cpp database.cpp pdf.cpp utils.cpp stb_impl.cpp third_party/TinyEXIF.cpp)
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
    target_compile_options(layout_debug_test PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# Incremental layout test (no GUI required)
if(JPEG_FOUND)
    add_executable(test_incremental_layout test_incremental_layout.cpp database.cpp utils.cpp stb_impl.cpp third_party/TinyEXIF.cpp)
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
    target_compile_options(test_incremental_layout PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# Incremental scan test (no GUI required)
if(JPEG_FOUND)
    add_executable(test_incremental_scan test_incremental_scan.cpp database.cpp utils.cpp stb_impl.cpp third_party/TinyEXIF.cpp)
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
    target_compile_options(test_incremental_scan PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
endif()

# New unified picexplore executable
if(JPEG_FOUND)
    add_executable(picexplore picexplore.cpp Fl_JustifiedLayout.cpp database.cpp pdf.cpp utils.cpp stb_impl.cpp third_party/TinyEXIF.cpp)
    target_include_directories(picexplore PRIVATE
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
    .)
    target_link_libraries(picexplore xxhash lmdb fltk ${JPEG_LIBRARIES})
    target_compile_options(picexplore PRIVATE -g -DTINYEXIF_NO_XMP_SUPPORT)
else()
    message(WARNING "libjpeg not found, skipping picexplore executable")
endif()

# Similarity search components (only build if mlpack is available)
if(MLPACK_FOUND)
    message(STATUS "Found mlpack, building similarity search components")

    # Image similarity index library
    add_library(similarity_index STATIC
        similarity/image_similarity_index.cpp
    )
    target_include_directories(similarity_index PRIVATE
        ${XXHASH_DIR}
        ${STB_DIR}
        ${LMDB_DIR}
        ${CONCURRENTQUEUE_DIR}
        ${MLPACK_INCLUDE_DIRS}
        .
    )
    target_link_libraries(similarity_index
        xxhash lmdb ${MLPACK_LIBRARIES} ${JPEG_LIBRARIES}
    )
    target_compile_options(similarity_index PRIVATE -g)

    # Similarity search command line tool
    add_executable(simfind similarity/simfind.cpp)
    target_include_directories(simfind PRIVATE
        ${CXXOPTS_DIR}/include
        ${CONCURRENTQUEUE_DIR}
        .
    )
    target_link_libraries(simfind similarity_index)
    target_compile_options(simfind PRIVATE -g)

else()
    message(WARNING "mlpack not found, skipping similarity search components")
    message(STATUS "Install libmlpack-dev to enable similarity search features")
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
