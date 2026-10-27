#pragma once

#include "Paths.h"

#include <QString>

// scripts/kbdinfo.sh (up to 0.5.x; now only in git history) in-process (SPEC.md K-1..K-5): read only, the same lines in the same order, so the parsing
// stays the QML's (KeyboardParse) and the two can be compared byte for byte (.dev/phase4diff.sh). Shell details
// kept on purpose (measured with /bin/sh = bash): globs sorted by the locale's collation (strcoll, as bash), `read -r`
// gives the first line without leading/trailing spaces and tabs, and a failed read keeps the variable's earlier value
// (the light loop never resets it). Runs on the keyboard's I/O thread: reading a light's file can go into its driver.
namespace KeyboardScan {
QString scan(const Paths &paths);
}
