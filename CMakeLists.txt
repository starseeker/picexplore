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
set(PICOJPEG_DIR ${CMAKE_SOURCE_DIR}/third_party/picojpeg)

# xxHash - compile the source
add_library(xxhash STATIC ${XXHASH_DIR}/xxhash.c)
target_include_directories(xxhash PUBLIC ${XXHASH_DIR})

# LMDB - compile the source
add_library(lmdb STATIC ${LMDB_DIR}/mdb.c ${LMDB_DIR}/midl.c)
target_include_directories(lmdb PUBLIC ${LMDB_DIR})
target_compile_definitions(lmdb PRIVATE MDB_MAXKEYSIZE=511)

# PicoJPEG - compile the source
add_library(picojpeg STATIC ${PICOJPEG_DIR}/picojpeg.c)
target_include_directories(picojpeg PUBLIC ${PICOJPEG_DIR})

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
    ${PICOJPEG_DIR}
)
target_link_libraries(image_thumb_db xxhash lmdb picojpeg ${JPEG_LIBRARIES} ${EXIF_LIBRARIES})
target_compile_options(image_thumb_db PRIVATE -g)

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
