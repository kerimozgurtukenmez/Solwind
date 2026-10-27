# Solwind's plugins: one folder per brand (NBFC is a brand of its own), one plugin per kernel module / driver inside.
# Each is a file solwindd loads at start (plugins/<Brand>/<name>.so in the build folder); none is installed with
# Solwind: `make plugin P=<Brand>/<name>` puts one into ~/.local/share/solwind/plugins (PLUGIN-PLAN.md).
#
# solwind_plugin(<Brand>/<name> SOURCES <its code>): the code as a static library <Brand>-<name>-code (the tests
# link it), Plugin.cpp (the class with Q_PLUGIN_METADATA) and plugin.json.in (its metadata; @SOLWIND_SDK_ID@ = the
# SDK it is built against: solwindd loads only plugins built for its own) as the plugin file. The file and its folders
# are made unwritable for group and others after every build (solwindd refuses a plugin others could change; a
# umask of 002 would leave them group-writable).
function(solwind_plugin path)
    cmake_parse_arguments(PARSE_ARGV 1 A "" "" "SOURCES")
    string(REPLACE "/" "-" target "${path}")
    get_filename_component(brand "${path}" DIRECTORY)
    get_filename_component(name "${path}" NAME)

    # Hidden symbols: plugins of different brands may hold classes of the same name (each brand has its own copy of a
    # standard interface); only Qt's plugin entry points are exported, so one plugin's classes never bind to another's
    add_library(${target}-code STATIC ${A_SOURCES})
    set_target_properties(${target}-code PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                                    VISIBILITY_INLINES_HIDDEN ON)
    target_include_directories(${target}-code PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target}-code PUBLIC solwind_sdk)

    configure_file(plugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/plugin.json" @ONLY)
    add_library(${target} MODULE Plugin.cpp)
    # (moc looks for plugin.json in the include folders)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${target} PRIVATE ${target}-code)
    # No RPATH: libsolwind-sdk.so is the one solwindd has loaded already (found by its name)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${name}"
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${brand}"
                          SKIP_BUILD_RPATH ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND chmod go-w "$<TARGET_FILE:${target}>" "${CMAKE_BINARY_DIR}/plugins/${brand}" "${CMAKE_BINARY_DIR}/plugins"
                       VERBATIM)
    set_property(GLOBAL APPEND PROPERTY SOLWIND_PLUGINS ${target})
endfunction()

add_subdirectory(NBFC/nbfc)
add_subdirectory(HP/hp-wmi-keyboard)
add_subdirectory(HP/hp-wmi-keyboard-zones)
add_subdirectory(HP/hp-wmi-rgb-zones)
