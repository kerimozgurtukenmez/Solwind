import Quickshell.Io
import QtQuick

// Runs a one-off command: go(["cmd", "arg"], (code, stdout, stderr) => { ... })
// The callback always runs exactly once, also when the command can't be started (code 127) or runs longer than
// `timeout` ms (it is stopped, code 124; 0 = no limit).
Process {
    id: p
    property var done: null
    // `running` may not be true right after `running = true`; use this to know whether it is busy
    property bool busy: false
    property int timeout: 0
    property int code: -1
    property bool gotExit: false
    property bool gotOut: false
    property bool gotErr: false
    property bool timedOut: false

    stdout: StdioCollector { id: so; onStreamFinished: { p.gotOut = true; p.fin() } }
    stderr: StdioCollector { id: se; onStreamFinished: { p.gotErr = true; p.fin() } }
    onExited: (exitCode, exitStatus) => { p.code = p.timedOut ? 124 : exitCode; p.gotExit = true; p.fin() }
    // A command that can't be started (not installed) only makes `running` go false: Quickshell sends no `exited`
    // and the streams never finish (src/io/process.cpp, onErrorOccurred). A normal end sends `exited` first.
    onRunningChanged: if (!running && busy && !gotExit) end(127, "", "failed to start: " + command[0])

    // SIGTERM when the time is up, SIGKILL 2 s later if it is still there
    property Timer guard: Timer {
        interval: Math.max(1, p.timeout)
        onTriggered: {
            if (!p.busy) return
            p.timedOut = true
            p.signal(15)
            p.killer.restart()
        }
    }
    property Timer killer: Timer { interval: 2000; onTriggered: if (p.busy && p.running) p.signal(9) }

    function fin() {
        if (!gotExit || !gotOut || !gotErr) return
        end(code, so.text, se.text)
    }
    function end(c, out, err) {
        if (!busy) return
        const cb = done
        done = null
        busy = false
        guard.stop()
        killer.stop()
        if (cb) cb(c, out, err)
    }
    function go(cmd, cb) {
        done = cb
        busy = true
        gotExit = false; gotOut = false; gotErr = false; timedOut = false; code = -1
        command = cmd
        running = true
        if (timeout > 0 && busy) guard.restart()
    }
}
