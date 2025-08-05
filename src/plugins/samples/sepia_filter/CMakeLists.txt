# Sample sepia filter plugin
add_library(sepia_filter_plugin SHARED
    sepia_filter_plugin.cpp
)

target_include_directories(sepia_filter_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(sepia_filter_plugin PRIVATE -g)

# Set the output name and location
set_target_properties(sepia_filter_plugin PROPERTIES
    OUTPUT_NAME "sepia_filter"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Install plugin to plugins directory
install(TARGETS sepia_filter_plugin
    LIBRARY DESTINATION lib/picexplore/plugins
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8