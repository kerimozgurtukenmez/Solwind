# solwindd, Solwind's background service (C++/Qt). Phase 1 of the move from QML to a C++ core.
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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus)
qt_standard_project_setup()
include(GNUInstallDirs)

# Everything but main(): the tests link the same code the daemon runs
add_library(solwind_core STATIC
    src/core/JsCompat.cpp
    src/core/Paths.cpp
    src/core/Settings.cpp
    src/core/UnixSignals.cpp
    src/dbus/BusGuard.cpp
    src/dbus/DaemonApi.cpp
)
target_include_directories(solwind_core PUBLIC src)
target_link_libraries(solwind_core PUBLIC Qt6::Core Qt6::DBus)
target_compile_definitions(solwind_core PUBLIC SOLWIND_VERSION="${SOLWIND_VERSION}"
                                               QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_compile_options(solwind_core PUBLIC -Wall -Wextra)

add_executable(solwindd src/main.cpp)
target_link_libraries(solwindd PRIVATE solwind_core)
install(TARGETS solwindd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

option(SOLWIND_BUILD_TESTS "Build the tests" ON)
if(SOLWIND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
