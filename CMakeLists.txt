# Minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

project(PEXP)  # Picture Explorer

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to use system libraries (default: OFF for self-contained builds)
option(USE_SYSTEM_JPEG "Use system libjpeg instead of bundled libjpeg-turbo" OFF)
option(USE_SYSTEM_EXIF "Use system libexif instead of bundled libexif" OFF)

if(USE_SYSTEM_JPEG OR USE_SYSTEM_EXIF)
    find_package(PkgConfig REQUIRED)
endif()

if(USE_SYSTEM_JPEG)
    pkg_check_modules(JPEG REQUIRED libjpeg)
else()
    # Use bundled libjpeg-turbo
    set(ENABLE_SHARED OFF CACHE BOOL "Build shared libraries" FORCE)
    set(ENABLE_STATIC ON CACHE BOOL "Build static libraries" FORCE)
    set(WITH_TURBOJPEG OFF CACHE BOOL "Include TurboJPEG API" FORCE)
    set(WITH_JAVA OFF CACHE BOOL "Build Java wrapper" FORCE)
    add_subdirectory(third_party/libjpeg-turbo)
    set(JPEG_LIBRARIES jpeg-static)
    set(JPEG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo/src ${CMAKE_BINARY_DIR}/third_party/libjpeg-turbo)
endif()

if(USE_SYSTEM_EXIF)
    pkg_check_modules(EXIF REQUIRED libexif)
else()
    # Use bundled libexif
    add_subdirectory(third_party/libexif)
    set(EXIF_LIBRARIES exif)
    set(EXIF_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/libexif)
endif()

# Third-party dependencies paths
set(XXHASH_DIR ${CMAKE_SOURCE_DIR}/third_party/xxHash)
set(STB_DIR ${CMAKE_SOURCE_DIR}/third_party/stb)
set(EPEG_DIR ${CMAKE_SOURCE_DIR}/third_party/epeg)
set(LMDB_DIR ${CMAKE_SOURCE_DIR}/third_party/lmdb/libraries/liblmdb)
set(CXXOPTS_DIR ${CMAKE_SOURCE_DIR}/third_party/cxxopts)
set(STRUETYPE_DIR ${CMAKE_SOURCE_DIR}/third_party/struetype)

# xxHash - compile the source
add_library(xxhash STATIC ${XXHASH_DIR}/xxhash.c)
target_include_directories(xxhash PUBLIC ${XXHASH_DIR})

# LMDB - compile the source
add_library(lmdb STATIC ${LMDB_DIR}/mdb.c ${LMDB_DIR}/midl.c)
target_include_directories(lmdb PUBLIC ${LMDB_DIR})
target_compile_definitions(lmdb PRIVATE MDB_MAXKEYSIZE=511)

# Epeg - use the existing CMakeLists.txt (requires libjpeg and libexif)
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)  
set(BUILD_PROGRAM OFF CACHE BOOL "" FORCE)

# Configure epeg to use our bundled libraries
if(NOT USE_SYSTEM_JPEG)
    # Set both the source and binary directories for libjpeg-turbo
    set(JPEG_INCLUDE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo/src CACHE PATH "" FORCE)
    set(JPEG_LIB_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/libjpeg-turbo CACHE PATH "" FORCE)
    # Also ensure jconfig.h is found by adding the binary directory to the include path
    include_directories(BEFORE SYSTEM ${CMAKE_BINARY_DIR}/third_party/libjpeg-turbo)
endif()

if(NOT USE_SYSTEM_EXIF)
    set(EXIF_INCLUDE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/libexif CACHE PATH "" FORCE)
    set(EXIF_LIB_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/libexif CACHE PATH "" FORCE)
endif()

add_subdirectory(${EPEG_DIR} epeg_build)

# Make sure epeg depends on our bundled libraries
if(NOT USE_SYSTEM_EXIF)
    add_dependencies(epeg_static exif)
endif()

# Original executable
add_executable(justified_layout_test justified_layout_test.cpp)
target_compile_options(justified_layout_test PRIVATE -g)

# New image_thumb_db executable
add_executable(image_thumb_db image_thumb_db.cpp)
target_include_directories(image_thumb_db PRIVATE 
    ${XXHASH_DIR}
    ${STB_DIR} 
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${EPEG_DIR}/src/lib
    ${JPEG_INCLUDE_DIRS}
    ${EXIF_INCLUDE_DIRS}
)
target_link_libraries(image_thumb_db xxhash lmdb epeg_static ${JPEG_LIBRARIES} ${EXIF_LIBRARIES})
target_compile_options(image_thumb_db PRIVATE -g)

# Add dependencies for bundled libraries
if(NOT USE_SYSTEM_EXIF)
    add_dependencies(image_thumb_db exif)
endif()

# New thumb_gallery_pdf executable
add_executable(thumb_gallery_pdf thumb_gallery_pdf.cpp)
target_include_directories(thumb_gallery_pdf PRIVATE 
    ${XXHASH_DIR}
    ${STB_DIR} 
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${STRUETYPE_DIR}
    ${STRUETYPE_DIR}/examples
)
target_link_libraries(thumb_gallery_pdf xxhash lmdb)
target_compile_options(thumb_gallery_pdf PRIVATE -g)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
