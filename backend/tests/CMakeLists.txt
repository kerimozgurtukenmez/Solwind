find_package(Qt6 REQUIRED COMPONENTS Test)

function(solwind_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE solwind_core Qt6::Test)
endfunction()

solwind_test(tst_jscompat)
add_test(NAME jscompat COMMAND tst_jscompat)

solwind_test(tst_settings)
add_test(NAME settings COMMAND tst_settings)

# Daemon tests start solwindd: always on a private session bus of their own (dbus-run-session), never the user's
solwind_test(tst_daemon)
target_compile_definitions(tst_daemon PRIVATE SOLWINDD_PATH="$<TARGET_FILE:solwindd>")
add_dependencies(tst_daemon solwindd)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME daemon COMMAND ${DBUS_RUN_SESSION} -- env SOLWIND_TEST_PRIVATE_BUS=1 $<TARGET_FILE:tst_daemon>)

# Not a test by itself: answers validator calls as JSON lines, so the same inputs can be compared with the old QML
# Settings.qml (the differential check in .dev, see SPEC.md ST-4)
add_executable(settings_probe settings_probe.cpp)
target_link_libraries(settings_probe PRIVATE solwind_core)
