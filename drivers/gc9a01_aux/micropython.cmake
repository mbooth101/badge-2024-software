# Create an INTERFACE library for our C module.
add_library(usermod_display_aux INTERFACE)

# Add our source files to the lib
target_sources(usermod_display_aux INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/display_aux.c
    ${CMAKE_CURRENT_LIST_DIR}/gc9a01.c
)

target_include_directories(usermod_display_aux INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_display_aux)
