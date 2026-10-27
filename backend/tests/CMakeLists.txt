find_package(Qt6 REQUIRED COMPONENTS Test)

# Not tests by themselves: they answer calls as JSON lines, so the same inputs can be compared with the old QML app
# (the differential checks in .dev, see SPEC.md ST-4): validators (settings_probe), fan control scenarios on a plugin
# that follows the QML's NBFC queue and status read (phase3_probe, .dev/phase3diff.sh)
add_executable(settings_probe settings_probe.cpp)
target_link_libraries(settings_probe PRIVATE solwind_core)
add_executable(phase3_probe phase3_probe.cpp)
target_link_libraries(phase3_probe PRIVATE solwind_core)

# Every test gets the fake programs (tests/fake: nbfc, systemctl, nvidia-smi, lspci) through TestEnv.h, and the
# plugins' code (linked in; solwindd started by a test loads the plugin files of this build: SOLWIND_PLUGIN_PATH).
# The tests of the plugins and of solwindd with them need a checkout of Solwind-plugins: -DSOLWIND_PLUGINS_DIR.
get_property(SOLWIND_PLUGINS GLOBAL PROPERTY SOLWIND_PLUGINS)
list(TRANSFORM SOLWIND_PLUGINS APPEND -code OUTPUT_VARIABLE SOLWIND_PLUGIN_CODE)
function(solwind_test name)
    add_executable(${name} ${name}.cpp TestEnv.h TestHost.h FakePlugin.h)
    target_link_libraries(${name} PRIVATE solwind_core ${SOLWIND_PLUGIN_CODE} Qt6::Test)
    target_compile_definitions(${name} PRIVATE FAKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fake"
                                               PLUGINS_DIR="${CMAKE_BINARY_DIR}/plugins")
endfunction()

foreach(t jscompat settings cmd reader sensors control fanstate alerts catalog)
    solwind_test(tst_${t})
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()
target_sources(tst_catalog PRIVATE ../src/cli/PluginTool.cpp)

set(_needed NBFC-nbfc HP-hp-wmi-keyboard HP-hp-wmi-keyboard-zones HP-hp-wmi-rgb-zones)
foreach(t IN LISTS _needed)
    if(NOT TARGET ${t})
        message(STATUS "Tests of the plugins and of solwindd with them skipped: no ${t} (-DSOLWIND_PLUGINS_DIR=<a checkout of Solwind-plugins>)")
        return()
    endif()
endforeach()
foreach(t parse monitor nbfcplugin keyboard)
    solwind_test(tst_${t})
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()

# Daemon tests start solwindd: always on a private session bus of their own (dbus-run-session), never the user's
# Plugin files for tst_daemon's loader checks (testplugins/<name>.so): built for another SDK (oldsdk), no kind in its
# metadata (nokind), a fan plugin file that makes no fan plugin (emptyfan)
foreach(p oldsdk nokind emptyfan)
    configure_file(testplugins/${p}/plugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/testplugins/${p}/plugin.json" @ONLY)
    add_library(tst_${p} MODULE testplugins/${p}/Plugin.cpp)
    target_include_directories(tst_${p} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/testplugins/${p}")
    target_link_libraries(tst_${p} PRIVATE solwind_sdk)
    set_target_properties(tst_${p} PROPERTIES PREFIX "" OUTPUT_NAME ${p} LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/testplugins"
                          SKIP_BUILD_RPATH ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endforeach()
solwind_test(tst_daemon)
target_compile_definitions(tst_daemon PRIVATE SOLWINDD_PATH="$<TARGET_FILE:solwindd>" TEST_PLUGINS="${CMAKE_CURRENT_BINARY_DIR}/testplugins")
add_dependencies(tst_daemon solwindd tst_oldsdk tst_nokind tst_emptyfan ${SOLWIND_PLUGINS})
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME daemon COMMAND ${DBUS_RUN_SESSION} -- env SOLWIND_TEST_PRIVATE_BUS=1 $<TARGET_FILE:tst_daemon>)
solwind_test(tst_controld)
target_compile_definitions(tst_controld PRIVATE SOLWINDD_PATH="$<TARGET_FILE:solwindd>")
add_dependencies(tst_controld solwindd ${SOLWIND_PLUGINS})
add_test(NAME controld COMMAND ${DBUS_RUN_SESSION} -- env SOLWIND_TEST_PRIVATE_BUS=1 $<TARGET_FILE:tst_controld>)

# Probes as above, with the plugins' code: the phase 2 functions (nbfc status, config curves, sensors) and the sensor
# reader (.dev/phase2diff.sh); the keyboard: detection, parsing, colour maths, effects (.dev/phase4diff.sh)
add_executable(phase2_probe phase2_probe.cpp)
target_link_libraries(phase2_probe PRIVATE solwind_core NBFC-nbfc-code)
add_executable(phase4_probe phase4_probe.cpp)
target_link_libraries(phase4_probe PRIVATE solwind_core ${SOLWIND_PLUGIN_CODE})
