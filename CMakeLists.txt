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

# xxHash - compile the source
add_library(xxhash STATIC ${XXHASH_DIR}/xxhash.c)
target_include_directories(xxhash PUBLIC ${XXHASH_DIR})

# LMDB - compile the source
add_library(lmdb STATIC ${LMDB_DIR}/mdb.c ${LMDB_DIR}/midl.c)
target_include_directories(lmdb PUBLIC ${LMDB_DIR})
target_compile_definitions(lmdb PRIVATE MDB_MAXKEYSIZE=511)

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