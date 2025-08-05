# Sample simple metadata plugin
add_library(simple_metadata_plugin SHARED
    simple_metadata_plugin.cpp
)

target_include_directories(simple_metadata_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(simple_metadata_plugin PRIVATE -g)

# Set the output name and location
set_target_properties(simple_metadata_plugin PROPERTIES
    OUTPUT_NAME "simple_metadata"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Install plugin to plugins directory
install(TARGETS simple_metadata_plugin
    LIBRARY DESTINATION lib/picexplore/plugins
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8