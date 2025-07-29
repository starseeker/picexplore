# Similarity search components (only build if mlpack is available)
if(MLPACK_FOUND)
    message(STATUS "Found mlpack, building similarity search components")

    # Image similarity index library
    add_library(similarity_index STATIC
        image_similarity_index.cpp
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
    add_executable(simfind simfind.cpp)
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
