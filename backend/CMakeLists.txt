# solwindd, Solwind's background service (C++/Qt): the C++ core that takes over from the QML services.
#   cmake -S backend -B backend/build && cmake --build backend/build && ctest --test-dir backend/build
# The daemon tests run on a private D-Bus session of their own (dbus-run-session), never on the user's.
cmake_minimum_required(VERSION 3.21)

# One version for the whole app: the repository's VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" SOLWIND_VERSION)
string(STRIP "${SOLWIND_VERSION}" SOLWIND_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
# project() takes numbers only; a suffix (0.6.0-rc1) stays in SOLWIND_VERSION
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" SOLWIND_VERSION_NUMBER "${SOLWIND_VERSION}")

project(solwindd VERSION ${SOLWIND_VERSION_NUMBER} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Gui only for QColor: the keyboard's colour maths must be the QML app's to the bit (no window, no QGuiApplication)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Gui)
qt_standard_project_setup()
include(GNUInstallDirs)

# The plugin SDK's id: the version plus a hash of the headers a plugin is built against (their classes' layout). A
# plugin carries the id it was built with and solwindd loads only its own: changed headers with the same VERSION (a
# development build) refuse old plugins instead of loading them. Editing a header reconfigures.
set(SOLWIND_SDK_HEADERS src/core/Cmd.h src/core/CommandQueue.h src/core/JsCompat.h src/core/Paths.h src/plugins/FanPlugin.h
    src/plugins/FanService.h src/plugins/KeyboardPlugin.h src/plugins/PluginHost.h src/plugins/SolwindPlugin.h)
set(_sdk_hashes "")
foreach(h IN LISTS SOLWIND_SDK_HEADERS)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${h}" _h)
    string(APPEND _sdk_hashes "${_h}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${h}")
endforeach()
string(SHA256 _sdk_hash "${_sdk_hashes}")
string(SUBSTRING "${_sdk_hash}" 0 12 _sdk_hash)
set(SOLWIND_SDK_ID "${SOLWIND_VERSION}+${_sdk_hash}")

# The plugin SDK (libsolwind-sdk.so): the plugin interfaces and what plugins share with the core. solwindd and every
# plugin it loads use this one copy (one QMetaObject per interface, one typeinfo, one Cmd); next to solwindd
add_library(solwind_sdk SHARED
    src/core/Cmd.cpp
    src/core/CommandQueue.cpp
    src/core/JsCompat.cpp
    src/core/Paths.cpp
    src/plugins/FanPlugin.h
    src/plugins/FanService.h
    src/plugins/KbdHelpers.cpp
    src/plugins/KeyboardPlugin.h
    src/plugins/PluginHost.h
    src/plugins/SolwindPlugin.h
)
set_target_properties(solwind_sdk PROPERTIES OUTPUT_NAME solwind-sdk LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(solwind_sdk PUBLIC src)
target_link_libraries(solwind_sdk PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(solwind_sdk PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII SOLWIND_SDK_ID="${SOLWIND_SDK_ID}"
                                       PRIVATE SOLWIND_SOURCE_APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(solwind_sdk PUBLIC -Wall -Wextra)

# Everything but main(): the tests link the same code the daemon runs
add_library(solwind_core STATIC
    src/core/Alerts.cpp
    src/core/FanController.cpp
    src/core/FanState.cpp
    src/core/HandBack.cpp
    src/core/I18n.cpp
    src/core/CaelestiaScheme.cpp
    src/core/CoreHost.h
    src/core/KbdIo.cpp
    src/core/KeyboardController.cpp
    src/core/KeyboardParse.cpp
    src/core/KeyboardScan.cpp
    src/core/NoFanPlugin.h
    src/core/Notifier.cpp
    src/core/OldAppGuard.cpp
    src/core/PluginLoader.cpp
    src/core/SensorReader.cpp
    src/core/Sensors.cpp
    src/core/Settings.cpp
    src/core/UnixSignals.cpp
    src/dbus/BusGuard.cpp
    src/dbus/ClientHints.cpp
    src/dbus/DBusUtil.cpp
    src/dbus/DaemonApi.cpp
    src/dbus/FansApi.cpp
    src/dbus/KeyboardApi.cpp
    src/dbus/SensorsApi.cpp
)
# The UI's translations, built in (one file for the QML app and the daemon): services/translations.js -> :/translations.js
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../services/translations.js" PROPERTIES QT_RESOURCE_ALIAS translations.js)
qt_add_resources(solwind_core translations PREFIX "/" FILES "${CMAKE_CURRENT_SOURCE_DIR}/../services/translations.js")
target_include_directories(solwind_core PUBLIC src)
target_link_libraries(solwind_core PUBLIC solwind_sdk Qt6::Core Qt6::DBus Qt6::Gui)
target_compile_definitions(solwind_core PUBLIC SOLWIND_VERSION="${SOLWIND_VERSION}"
                                               SOLWIND_SOURCE_APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
                                               QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_compile_options(solwind_core PUBLIC -Wall -Wextra)

add_executable(solwindd src/main.cpp src/cli/CallClient.cpp)
target_link_libraries(solwindd PRIVATE solwind_core)
# libsolwind-sdk.so lies next to it (build and install): that folder, and nothing else
set_target_properties(solwindd PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "\$ORIGIN")
install(TARGETS solwindd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_subdirectory(qml)
# The plugins (none built in: NBFC, HP ...), each its own file solwindd loads: <build>/plugins/<Brand>/<name>.so (their
# CMake files in <build>/plugin-build, so that folder holds only the plugin files)
add_subdirectory(plugins plugin-build)

option(SOLWIND_BUILD_TESTS "Build the tests" ON)
if(SOLWIND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
