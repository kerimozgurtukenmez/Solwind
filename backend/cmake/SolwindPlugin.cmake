# solwind_plugin(<Brand>/<name> SOURCES <its code>): a Solwind plugin file, <build>/plugins/<Brand>/<name>.so, from the
# plugin's folder (the CMakeLists.txt calling it): the code as a static library <Brand>-<name>-code (Solwind's tests
# link it), Plugin.cpp (the class with Q_PLUGIN_METADATA) and plugin.json.in (its metadata; @SOLWIND_SDK_ID@ = the SDK
# it is built against: solwindd loads only plugins built for its own) as the plugin file. Part of the SDK
# (find_package(SolwindSdk)); Solwind's own build includes it too.
#
# Hidden symbols: plugins of different brands may hold classes of the same name (each brand has its own copy of a
# standard interface); only Qt's plugin entry points are exported, so one plugin's classes never bind to another's.
# The file and its folders are made unwritable for group and others after every build (solwindd refuses a plugin
# others could change; a umask of 002 would leave them group-writable).
function(solwind_plugin path)
    cmake_parse_arguments(PARSE_ARGV 1 A "" "" "SOURCES")
    if(NOT path MATCHES "^[A-Za-z0-9][A-Za-z0-9_.+-]*/[A-Za-z0-9][A-Za-z0-9_.+-]*$")
        message(FATAL_ERROR "solwind_plugin: '${path}' isn't a plugin id (<Brand>/<name>)")
    endif()
    if(NOT SOLWIND_SDK_ID)
        message(FATAL_ERROR "solwind_plugin: no SOLWIND_SDK_ID (find_package(SolwindSdk) first)")
    endif()
    string(REPLACE "/" "-" target "${path}")
    get_filename_component(brand "${path}" DIRECTORY)
    get_filename_component(name "${path}" NAME)
    if(NOT SOLWIND_PLUGIN_OUTPUT_DIR)
        set(SOLWIND_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
    endif()

    add_library(${target}-code STATIC ${A_SOURCES})
    set_target_properties(${target}-code PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                                    VISIBILITY_INLINES_HIDDEN ON AUTOMOC ON)
    target_compile_features(${target}-code PUBLIC cxx_std_20)
    target_include_directories(${target}-code PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target}-code PUBLIC Solwind::sdk)

    configure_file(plugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/plugin.json" @ONLY)
    add_library(${target} MODULE Plugin.cpp)
    # (moc looks for plugin.json in the include folders)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${target} PRIVATE ${target}-code)
    # No RPATH: libsolwind-sdk.so is the one solwindd has loaded already (found by its name)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME "${name}" AUTOMOC ON
                          LIBRARY_OUTPUT_DIRECTORY "${SOLWIND_PLUGIN_OUTPUT_DIR}/${brand}"
                          SKIP_BUILD_RPATH ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND chmod go-w "$<TARGET_FILE:${target}>" "${SOLWIND_PLUGIN_OUTPUT_DIR}/${brand}"
                               "${SOLWIND_PLUGIN_OUTPUT_DIR}"
                       VERBATIM)
    set_property(GLOBAL APPEND PROPERTY SOLWIND_PLUGINS ${target})
endfunction()
