# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

project(picexplore)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required system packages
find_package(FLTK REQUIRED)

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
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(JPEG REQUIRED libjpeg)
        pkg_check_modules(PNG REQUIRED libpng)
        pkg_check_modules(ZLIB REQUIRED zlib)
        pkg_check_modules(WEBP REQUIRED libwebp)
        pkg_check_modules(TIFF REQUIRED libtiff-4)
    else()
        find_package(JPEG REQUIRED)
        find_package(PNG REQUIRED)
        find_package(ZLIB REQUIRED)
        find_package(WebP REQUIRED)
        find_package(TIFF REQUIRED)
        set(JPEG_LIBRARIES ${JPEG_LIBRARIES})
        set(JPEG_INCLUDE_DIRS ${JPEG_INCLUDE_DIR})
        set(PNG_LIBRARIES ${PNG_LIBRARIES})
        set(PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIR})
        set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES})
        set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})
        set(WEBP_LIBRARIES ${WEBP_LIBRARIES})
        set(WEBP_INCLUDE_DIRS ${WEBP_INCLUDE_DIRS})
        set(TIFF_LIBRARIES ${TIFF_LIBRARIES})
        set(TIFF_INCLUDE_DIRS ${TIFF_INCLUDE_DIR})
    endif()
endif()

# Third-party dependencies paths
set(XXHASH_DIR ${CMAKE_SOURCE_DIR}/third_party/xxHash)
set(STB_DIR ${CMAKE_SOURCE_DIR}/third_party/stb)
set(LMDB_DIR ${CMAKE_SOURCE_DIR}/third_party/lmdb/libraries/liblmdb)
set(CXXOPTS_DIR ${CMAKE_SOURCE_DIR}/third_party/cxxopts)
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

# Single unified picexplore executable
add_executable(picexplore
    src/main.cpp
    src/cli/cli_scan.cpp
    src/cli/pdf.cpp
    src/gui/main_window.cpp
    src/gui/virtual_viewport.cpp
    src/gui/scan_coordinator.cpp
    src/gui/thumbnail_pipeline.cpp
    src/gui/image_store.cpp
    src/gui/layout_engine.cpp
    src/gui/info_panel.cpp
    src/gui/full_res_loader.cpp
    src/gui/tile_manager.cpp
    src/gui/inotify_watcher.cpp
    src/database.cpp
    src/utils.cpp
    third_party/TinyEXIF.cpp
)

target_include_directories(picexplore PRIVATE
    ${FLTK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/cli
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}
    ${XXHASH_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${CONCURRENTQUEUE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
    ${JPEG_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${WEBP_INCLUDE_DIRS}
    ${TIFF_INCLUDE_DIRS}
)

target_link_libraries(picexplore
    ${FLTK_LIBRARIES}
    xxhash
    lmdb
    ${JPEG_LIBRARIES}
    ${PNG_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${WEBP_LIBRARIES}
    ${TIFF_LIBRARIES}
)

target_compile_definitions(picexplore PRIVATE
    _LARGEFILE64_SOURCE
    _FILE_OFFSET_BITS=64
    TINYEXIF_NO_XMP_SUPPORT
)

target_compile_options(picexplore PRIVATE -g -O2)
