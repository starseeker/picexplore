# picexplore executable
if(JPEG_FOUND)
    add_executable(picexplore picexplore.cpp Fl_JustifiedLayout.cpp database.cpp pdf.cpp utils.cpp stb_impl.cpp ../third_party/TinyEXIF.cpp)
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

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
