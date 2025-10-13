# CMakeLists.txt for GUI components

# Find FLTK
find_package(FLTK REQUIRED)

# GUI executable
add_executable(picexplore_gui
    picexplore_gui.cpp
    fl_justified_gallery.cpp
    thumbnail_cache.cpp
    stb_impl.cpp
)

target_include_directories(picexplore_gui PRIVATE
    ${FLTK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${JPEG_INCLUDE_DIRS}
)

target_link_libraries(picexplore_gui
    ${FLTK_LIBRARIES}
    ${JPEG_LIBRARIES}
)

target_compile_options(picexplore_gui PRIVATE -g)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
