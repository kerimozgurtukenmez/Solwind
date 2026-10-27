#pragma once

#include <QStringList>

// `solwindd --call <object> <method> [<signature> <argument>...]` and `solwindd --get <object> <property>`: the
// terminal commands' way to a running solwindd (bin/solwind), without a D-Bus tool whose output would need parsing.
// object: daemon | fans | sensors | keyboard. The signature gives each argument's D-Bus type (s i u x d b, one per
// argument). A string answer is printed as it is, others as text; an error's message goes to stderr.
namespace CallClient {

enum Exit { Ok = 0, Refused = 1, Usage = 2, NotRunning = 4 };

// args: what follows --call / --get. Returns the exit code.
int call(const QStringList &args);
int get(const QStringList &args);

} // namespace CallClient
