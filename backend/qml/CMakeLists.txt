# The QML app's D-Bus client (import Solwind): a QML plugin, loaded by Quickshell from QML_IMPORT_PATH
# (<build>/qml while developing; installed with the app in phase 6). It doesn't link the daemon's code.
find_package(Qt6 REQUIRED COMPONENTS Qml)
qt_add_qml_module(solwindqml
    URI Solwind
    VERSION 1.0
    PLUGIN_TARGET solwindqml
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Solwind"
    SOURCES SolwindBus.h SolwindBus.cpp
)
target_link_libraries(solwindqml PRIVATE Qt6::Qml Qt6::DBus)
target_compile_definitions(solwindqml PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_compile_options(solwindqml PRIVATE -Wall -Wextra)
