pragma Singleton
import Quickshell
import QtQuick

// Desktop notifications (notify-send -> notification server; with Caelestia, Caelestia's notifications)
Singleton {
    id: root
    readonly property bool enabled: Settings.notifyOn("enabled")       // master switch
    readonly property bool profiles: Settings.notifyOn("profile")      // profile changed from a keybind/terminal

    // urgency: "low" | "normal" | "critical"
    function send(title, body, urgency, icon) {
        if (!enabled) return
        Quickshell.execDetached(["notify-send", "-a", "Solwind", "-u", urgency || "normal",
                                 "-i", icon || "dialog-information", title, body || ""])
    }
}
