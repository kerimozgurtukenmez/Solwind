find_package(Qt6 REQUIRED COMPONENTS Test)

# Every test gets the fake programs (tests/fake: nbfc, systemctl, nvidia-smi, lspci) through TestEnv.h
function(solwind_test name)
    add_executable(${name} ${name}.cpp TestEnv.h FakePlugin.h)
    target_link_libraries(${name} PRIVATE solwind_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE FAKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fake")
endfunction()

foreach(t jscompat settings cmd parse reader sensors monitor control fanstate nbfcplugin alerts)
    solwind_test(tst_${t})
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()

# Daemon tests start solwindd: always on a private session bus of their own (dbus-run-session), never the user's
solwind_test(tst_daemon)
target_compile_definitions(tst_daemon PRIVATE SOLWINDD_PATH="$<TARGET_FILE:solwindd>")
add_dependencies(tst_daemon solwindd)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME daemon COMMAND ${DBUS_RUN_SESSION} -- env SOLWIND_TEST_PRIVATE_BUS=1 $<TARGET_FILE:tst_daemon>)
solwind_test(tst_controld)
target_compile_definitions(tst_controld PRIVATE SOLWINDD_PATH="$<TARGET_FILE:solwindd>")
add_dependencies(tst_controld solwindd)
add_test(NAME controld COMMAND ${DBUS_RUN_SESSION} -- env SOLWIND_TEST_PRIVATE_BUS=1 $<TARGET_FILE:tst_controld>)

# Not a test by itself: answers validator calls as JSON lines, so the same inputs can be compared with the old QML
# Settings.qml (the differential check in .dev, see SPEC.md ST-4)
add_executable(settings_probe settings_probe.cpp)
target_link_libraries(settings_probe PRIVATE solwind_core)

# The same for the phase 2 functions (nbfc status, config curves, sensors) and the sensor reader (.dev/phase2diff.sh)
add_executable(phase2_probe phase2_probe.cpp)
target_link_libraries(phase2_probe PRIVATE solwind_core)

# The same for fan control scenarios: FanController + FanState + Settings on a plugin that follows the QML's NBFC
# queue and status read (.dev/phase3diff.sh)
add_executable(phase3_probe phase3_probe.cpp)
target_link_libraries(phase3_probe PRIVATE solwind_core)
