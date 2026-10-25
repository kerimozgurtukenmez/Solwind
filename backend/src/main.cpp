// solwindd: Solwind's background service. Runs as the user (never as root), owns the D-Bus name solwind.Daemon.
// Phase 2 of ARCH-PLAN.md: settings, D-Bus, clean quit, and reading: sensors and NBFC's state ("observe" mode).
// Nothing here writes to fans, lights or files yet; it can run next to the QML app.
#include "core/History.h"
#include "core/Paths.h"
#include "core/Sensors.h"
#include "core/Settings.h"
#include "core/UnixSignals.h"
#include "dbus/BusGuard.h"
#include "dbus/ClientHints.h"
#include "dbus/DBusUtil.h"
#include "dbus/DaemonApi.h"
#include "dbus/FansApi.h"
#include "dbus/SensorsApi.h"
#include "plugins/fan/nbfc/NbfcMonitor.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDateTime>

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
    DBusUtil::registerTypes();
    ClientHints hints(bus);
    Sensors sensors(paths);
    NbfcMonitor nbfc(paths);
    SampleHistory history;
    DaemonApi api(paths, settings, loaded, &hints);
    SensorsApi sensorsApi(sensors);
    FansApi fansApi(nbfc, sensors, history);
    // The objects first, then the name: a client that sees the name can call them right away
    const struct { const char *path; QObject *obj; } objects[] = {
        { DaemonApi::objectPath, &api }, { SensorsApi::path, &sensorsApi }, { FansApi::path, &fansApi } };
    for (const auto &o : objects) {
        if (!bus.registerObject(QString::fromLatin1(o.path), o.obj,
                                QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals
                                    | QDBusConnection::ExportAllProperties)) {
            std::fprintf(stderr, "solwindd: could not register %s on D-Bus\n", o.path);
            return Error;
        }
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

    // ---------- reading ----------
    PropertyPublisher sensorsProps(bus, QString::fromLatin1(SensorsApi::path), u"solwind.Sensors"_s, &sensorsApi);
    PropertyPublisher fansProps(bus, QString::fromLatin1(FansApi::path), u"solwind.Fans"_s, &fansApi);
    // How often (SPEC.md P-2): fast while a window is open (later also while Solwind drives fans); the sensors are
    // read while the fan page is open, or notifications are on (the critical temperature warning needs them).
    // Phase 2 doesn't drive fans, and reads the settings once at start.
    const bool controlling = false, needsSensors = false;
    auto pace = [&] {
        const bool window = hints.has(u"window"_s);
        nbfc.setFast(window || controlling);
        sensors.setFast(window || controlling);
        sensors.setActive(hints.has(u"fans"_s) || needsSensors || settings.notifyOn(u"enabled"_s));
    };
    QObject::connect(&hints, &ClientHints::changed, &app, pace);
    QObject::connect(&sensors, &Sensors::changed, &app, [&] { sensorsProps.publish(); fansProps.publish(); });
    QObject::connect(&nbfc, &NbfcMonitor::changed, &app, [&] { fansProps.publish(); });
    QObject::connect(&nbfc, &NbfcMonitor::statusRead, &app, [&](bool ok) {
        if (!ok) return;
        QList<double> speeds;
        for (const auto &f : nbfc.fans()) speeds << f.current;
        const auto &s = history.record(QDateTime::currentMSecsSinceEpoch(), nbfc.temp(), sensors.state().gpu(), speeds);
        emit fansApi.Sampled(FansApi::sampleMap(s.t, s.cpu, s.gpu, s.fan));
    });
    api.extraStatus = [&] {
        return QVariantMap{ { u"nbfcPollMs"_s, nbfc.interval() }, { u"sensorsActive"_s, sensors.active() },
                            { u"sensorsFast"_s, sensors.fast() } };
    };
    pace();
    sensors.start();                              // only now: a second copy that exits with 3 reads nothing
    nbfc.start();

    std::fprintf(stderr, "solwindd %s started, observe mode (settings: %s%s)\n", SOLWIND_VERSION,
                 qPrintable(settings.path()), loaded ? "" : ", not read: defaults");
    const int rc = app.exec();
    settings.flush();
    if (bus.isConnected()) bus.unregisterService(name);
    return rc == 0 ? exitCode : Error;
}
