#pragma once

#include <QString>
#include <QStringList>

// Handing the fans back after solwindd has gone (a crash, a timeout, a stop): the systemd unit's ExecStopPost runs
// `solwindd --hand-back`, which runs the command the active fan plugin gave for it (FanPlugin::handBackCommand(),
// NBFC: nbfc set -a). solwindd records that command when it starts, once it owns its D-Bus name (so a second copy
// never touches it): $XDG_RUNTIME_DIR/solwind/fan-hand-back (tests: $SOLWIND_RUNTIME_DIR/fan-hand-back), one argument
// per line. No fan plugin, or one whose fans need nothing (the firmware takes them back by itself), or observe mode: no
// file, nothing is run.
namespace HandBack {
QString path();                                  // "" without XDG_RUNTIME_DIR
// Writes the command (atomically, only you can read or change it), or removes the file for an empty one. false and
// `error` if that failed.
bool record(const QStringList &command, QString *error = nullptr);
// `solwindd --hand-back`: runs the recorded command (no shell; at most 15 s). Nothing when the unit's EXIT_STATUS is 3
// (solwindd didn't start because something else drives the fans) or when there is no file. A file others could have
// changed is refused. Exit code: 0 done or nothing to do, 1 refused or the command failed.
int run();
}
