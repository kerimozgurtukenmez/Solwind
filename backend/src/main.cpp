// solwindd: Solwind's background service. Runs as the user (never as root), owns the D-Bus name solwind.Daemon.
// Phase 1 of ARCH-PLAN.md: settings, D-Bus, clean quit. Nothing here writes to fans or lights yet.
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/UnixSignals.h"
#include "dbus/BusGuard.h"
#include "dbus/DaemonApi.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>

#include <cstdio>

using namespace Qt::StringLiterals;

// Exit codes, as the solwind command uses them: 0 done, 1 error, 3 not started because Solwind already runs
enum Exit { Ok = 0, Error = 1, AlreadyRunning = 3 };

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"solwindd"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(SOLWIND_VERSION));

    QCommandLineParser cli;
    cli.setApplicationDescription(u"Solwind background service (fans and keyboard light). Control it with: solwind"_s);
    cli.addHelpOption();
    cli.addVersionOption();
    cli.process(app);

    // Signals first: a stop request during start-up (systemd stopping it right away) must still end in a clean quit.
    // The handler only queues it; it is acted on once the event loop runs.
    UnixSignals signals_;

    const Paths paths = Paths::fromEnvironment();
    Settings settings(paths.settingsDir);
    const bool loaded = settings.load();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "solwindd: no D-Bus session bus: %s\n", qPrintable(bus.lastError().message()));
        return Error;
    }
    const QString name = QString::fromLatin1(DaemonApi::serviceName);
    DaemonApi api(paths, settings, loaded);
    // The object first, then the name: a client that sees the name can call it right away
    if (!bus.registerObject(QString::fromLatin1(DaemonApi::objectPath), &api,
                            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableProperties)) {
        std::fprintf(stderr, "solwindd: could not register %s on D-Bus\n", DaemonApi::objectPath);
        return Error;
    }
    if (!bus.registerService(name)) {
        // Taken: another solwindd runs, and two copies must never drive the fans at once. Any other failure is an
        // error (not 3, which the service doesn't restart after).
        if (bus.interface()->isServiceRegistered(name)) {
            std::fprintf(stderr, "solwindd: already running (the D-Bus name %s is taken)\n", DaemonApi::serviceName);
            return AlreadyRunning;
        }
        std::fprintf(stderr, "solwindd: could not get the D-Bus name %s: %s\n", DaemonApi::serviceName,
                     qPrintable(bus.lastError().message()));
        return Error;
    }

    // A clean quit, however it is asked for: pending settings are written (later phases: fans back to NBFC, the
    // keyboard back to its static colour)
    bool quitting = false;
    int exitCode = Ok;
    auto quit = [&](int code) {
        if (quitting) return;
        quitting = true;
        exitCode = code;
        settings.flush();
        QCoreApplication::quit();
    };
    QObject::connect(&signals_, &UnixSignals::received, &app, [&](int signo) {
        std::fprintf(stderr, "solwindd: signal %d, quitting\n", signo);
        quit(Ok);
    });
    QObject::connect(&api, &DaemonApi::quitRequested, &app, [&] { quit(Ok); });
    BusGuard guard(bus, name);
    QObject::connect(&guard, &BusGuard::lost, &app, [&](const QString &why) {
        std::fprintf(stderr, "solwindd: %s, quitting\n", qPrintable(why));
        quit(Error);
    });

    std::fprintf(stderr, "solwindd %s started (settings: %s%s)\n", SOLWIND_VERSION, qPrintable(settings.path()),
                 loaded ? "" : ", not read: defaults");
    const int rc = app.exec();
    settings.flush();
    if (bus.isConnected()) bus.unregisterService(name);
    return rc == 0 ? exitCode : Error;
}
