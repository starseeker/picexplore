# CMakeLists.txt for GUI components

find_package(FLTK REQUIRED)

add_executable(picexplore_gui
    main.cpp
    main_window.cpp
    virtual_viewport.cpp
    scan_coordinator.cpp
    thumbnail_pipeline.cpp
    image_store.cpp
    layout_engine.cpp
    info_panel.cpp
    full_res_loader.cpp
    inotify_watcher.cpp
    ../third_party/miniz/miniz.c
    ../database.cpp
    ../utils.cpp
    ../third_party/TinyEXIF.cpp
)

target_include_directories(picexplore_gui PRIVATE
    ${FLTK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${XXHASH_DIR}
    ${STB_DIR}
    ${LMDB_DIR}
    ${CXXOPTS_DIR}/include
    ${STRUETYPE_DIR}
    ${STRUETYPE_DIR}/examples
    ${CONCURRENTQUEUE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
    ${JPEG_INCLUDE_DIRS}
    .
)

target_link_libraries(picexplore_gui
    ${FLTK_LIBRARIES}
    xxhash
    lmdb
    ${JPEG_LIBRARIES}
)

target_compile_options(picexplore_gui PRIVATE -g -O2 -DTINYEXIF_NO_XMP_SUPPORT)
