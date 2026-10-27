// solwindd: Solwind's background service. Runs as the user (never as root), owns the D-Bus name solwind.Daemon.
// Control mode (default): restores the saved fan settings and drives the fans through the NBFC plugin, with the
// safety limit, max fan, profiles and the NBFC service actions; quitting hands the fans back. Observe mode
// (--observe): reads only - no fan command, no settings write, no notification - so it can run next to the QML app.
#include "core/Alerts.h"
#include "core/FanController.h"
#include "core/FanState.h"
#include "core/History.h"
#include "core/I18n.h"
#include "core/KeyboardController.h"
#include "core/Notifier.h"
#include "core/OldAppGuard.h"
#include "core/Paths.h"
#include "core/Sensors.h"
#include "core/Settings.h"
#include "core/UnixSignals.h"
#include "dbus/BusGuard.h"
#include "dbus/ClientHints.h"
#include "dbus/DBusUtil.h"
#include "dbus/DaemonApi.h"
#include "dbus/FansApi.h"
#include "dbus/KeyboardApi.h"
#include "dbus/SensorsApi.h"
#include "plugins/fan/nbfc/NbfcPlugin.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDateTime>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

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
    const QCommandLineOption observeOpt(u"observe"_s, u"Only read: never write to fans or settings (next to the QML app)."_s);
    cli.addOption(observeOpt);
    cli.process(app);
    const bool control = !cli.isSet(observeOpt);

    // Signals first: a stop request during start-up (systemd stopping it right away) must still end in a clean quit.
    // The handler only queues it; it is acted on once the event loop runs.
    UnixSignals signals_;

    // TRANSITION (until phase 5): never drive the fans while the QML app does
    if (control) {
        const OldAppGuard::Result old = OldAppGuard::findNow();
        if (!old.ok) {
            std::fprintf(stderr, "solwindd: could not check whether the QML Solwind app runs (%s); try again, or start "
                                 "solwindd with --observe\n", qPrintable(old.error));
            return Error;
        }
        if (!old.path.isEmpty()) {
            std::fprintf(stderr, "solwindd: the QML Solwind app runs (%s) and drives the fans; quit it first, or start "
                                 "solwindd with --observe\n", qPrintable(old.path));
            return AlreadyRunning;
        }
    }

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
    I18n i18n;
    i18n.savedLanguage = [&] { return settings.uiLang(); };
    Notifier notifier(bus);
    notifier.enabled = [&] { return settings.notifyOn(u"enabled"_s); };
    ClientHints hints(bus);
    Sensors sensors(paths);
    NbfcPlugin nbfc(paths, i18n);
    FanController fans(nbfc, i18n);
    fans.gpuTemp = [&] { return sensors.state().gpu(); };
    fans.setSafety(std::isfinite(settings.safety()) ? settings.safety() : 90);
    FanState fanState(nbfc, fans, settings, i18n, notifier);
    // Notifications only in control mode: in observe mode the QML app next to it sends its own
    std::optional<Alerts> alerts;
    if (control) {
        alerts.emplace(nbfc, fans, i18n, notifier);
        alerts->sensorCpu = [&] { return sensors.state().cpu(); };
        alerts->sensorGpu = [&] { return sensors.state().gpu(); };
    }
    SampleHistory history;
    KeyboardController keyboard(paths, settings, i18n, control);
    KeyboardApi keyboardApi(keyboard, i18n);
    DaemonApi api(paths, settings, i18n, loaded, &hints);
    api.controlMode = control;
    SensorsApi sensorsApi(sensors);
    FansApi fansApi(nbfc, sensors, history, fans, fanState, i18n, settings, control);
    // The objects first, then the name: a client that sees the name can call them right away
    const struct { const char *path; QObject *obj; } objects[] = {
        { DaemonApi::objectPath, &api }, { SensorsApi::path, &sensorsApi }, { FansApi::path, &fansApi },
        { KeyboardApi::path, &keyboardApi } };
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

    // ---------- quitting (QT-1..QT-3) ----------
    // However it is asked for: pending settings written, nothing written to the fans any more, the fans Solwind drives
    // back to automatic, then exit. At most 20 s (like the terminal supervisor): a hanging command doesn't keep it.
    bool quitting = false;
    int exitCode = Ok;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(20000);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&] {
        // At once, without the destructors: one of them may be waiting for a driver call that hangs (the keyboard's
        // I/O thread). Settings were written when quitting began; the bus name goes with the process.
        std::fprintf(stderr, "solwindd: quitting took more than 20 s (a fan command or a keyboard light that hangs), quitting anyway\n");
        std::fflush(stderr);
        std::_Exit(exitCode == Ok ? Error : exitCode);
    });
    auto quit = [&](int code) {
        if (quitting) return;
        quitting = true;
        exitCode = code;
        nbfc.setQuitting(true);
        settings.flush();
        watchdog.start();
        // The keyboard back to its static colour and the fans back to automatic, side by side (deliberate: the QML
        // did the keyboard first, and a light that hangs must not keep the fans from being handed back); exit once both
        // are done
        auto left = std::make_shared<int>(2);
        auto oneDone = [&, left] { if (--*left == 0) QCoreApplication::exit(exitCode); };
        // (stepping aside for the QML app: the light is its now, nothing is written into its start)
        keyboard.restoreForQuit(oneDone, code != AlreadyRunning);
        fans.quit([&, oneDone](const FanController::QuitResult &r) {
            if (r.handBackFailed) {
                // Nothing watches the temperature after this but the config's critical limit: said even with
                // notifications off, and in the log
                std::fprintf(stderr, "[solwind] the fans could not be handed back to NBFC's automatic control (%s); they may "
                                     "stay at a fixed speed. Run `nbfc set -a` or open Solwind.\n", qPrintable(r.error));
                notifier.send(i18n.t(u"Fans may stay at a fixed speed"_s),
                              i18n.t(u"Solwind could not hand the fans back to NBFC's automatic control (%1). Run `nbfc set -a` or open Solwind."_s).arg(r.error),
                              Notifier::Critical, u"dialog-warning"_s, true, true);
                if (exitCode == Ok) exitCode = Error;
            }
            if (r.fixedSpeedsSaved) {
                // NBFC sets these again when it starts, with nothing watching the temperature but the config's
                // critical limit: said even with notifications off, and in the log
                std::fprintf(stderr, "[solwind] NBFC is stopped with fixed fan speeds saved in /var/lib/nbfc/state.json; "
                                     "it sets them again when it starts. After starting it, run `nbfc set -a` or open Solwind.\n");
                notifier.send(i18n.t(u"Fans may stay at a fixed speed"_s),
                              i18n.t(u"NBFC is stopped and has saved fixed fan speeds; it sets them again when it starts. After starting it, run `nbfc set -a` or open Solwind."_s),
                              Notifier::Normal, u"dialog-warning"_s, true, true);
            }
            oneDone();
        });
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
    OldAppGuard oldApp;
    if (control) {
        QObject::connect(&oldApp, &OldAppGuard::found, &app, [&](const QString &p) {
            std::fprintf(stderr, "solwindd: the QML Solwind app started (%s): handing the fans back to it and quitting\n",
                         qPrintable(p));
            quit(AlreadyRunning);
        });
        QObject::connect(&oldApp, &OldAppGuard::checkFailed, &app, [&](const QString &why) {
            std::fprintf(stderr, "solwindd: a Quickshell instance started and it could not be checked whether it is the "
                                 "QML Solwind app (%s): handing the fans back and quitting, to be safe\n", qPrintable(why));
            quit(Error);
        });
        oldApp.start();
    }

    // ---------- reading, and (control mode) driving ----------
    PropertyPublisher sensorsProps(bus, QString::fromLatin1(SensorsApi::path), u"solwind.Sensors"_s, &sensorsApi);
    PropertyPublisher fansProps(bus, QString::fromLatin1(FansApi::path), u"solwind.Fans"_s, &fansApi);
    PropertyPublisher keyboardProps(bus, QString::fromLatin1(KeyboardApi::path), u"solwind.Keyboard"_s, &keyboardApi);
    PropertyPublisher daemonProps(bus, QString::fromLatin1(DaemonApi::objectPath), u"solwind.Daemon"_s, &api);
    QObject::connect(&keyboard, &KeyboardController::changed, &app, [&] { keyboardProps.publish(); });
    // How often (SPEC.md P-1, P-2): fast while a window is open or Solwind drives fans; the sensors are read while
    // the fan page is open, Solwind drives fans in Fixed/Curve mode (the safety limit watches the GPU too), or
    // notifications are on (the critical temperature warning needs them)
    auto pace = [&] {
        const bool window = hints.has(u"window"_s);
        nbfc.monitor().setFast(window || fans.controlling());
        sensors.setFast(window || fans.controlling());
        sensors.setActive(hints.has(u"fans"_s) || fans.needsSensors() || (control && settings.notifyOn(u"enabled"_s)));
    };
    QObject::connect(&hints, &ClientHints::changed, &app, pace);
    QObject::connect(&sensors, &Sensors::changed, &app, [&] {
        if (alerts) alerts->update();
        sensorsProps.publish();
        fansProps.publish();
    });
    QObject::connect(&nbfc, &FanPlugin::changed, &app, [&] { fansProps.publish(); });
    QObject::connect(&fans, &FanController::changed, &app, [&] { pace(); fansProps.publish(); });
    QObject::connect(&fanState, &FanState::changed, &app, [&] { fansProps.publish(); });
    // (the language too: profile labels and keyboard texts are in it)
    QObject::connect(&settings, &Settings::changed, &app, [&] {
        pace();
        fansProps.publish();
        keyboardProps.publish();
        daemonProps.publish();
    });
    if (control) {
        // Another config: the old one's settings don't belong to these fans (CF-1); the new one's are restored on the
        // next read. Emitted inside the status read, before the fans are taken over (N-8, N-14).
        QObject::connect(&nbfc, &FanPlugin::configIdChanged, &app, [&] { fans.forgetFans(); fanState.unrestore(); });
    }
    QObject::connect(&nbfc, &FanPlugin::statusRead, &app, [&](bool ok) {
        if (ok) {
            // N-12: max fan's end, the loop, the graph; the saved settings right after (the QML's Qt.callLater)
            if (control) {
                fans.checkBoost();
                fans.control();
            }
            QList<double> speeds;
            for (const auto &f : nbfc.fans()) speeds << f.current;
            const auto &s = history.record(QDateTime::currentMSecsSinceEpoch(), nbfc.temp(), sensors.state().gpu(), speeds);
            emit fansApi.Sampled(FansApi::sampleMap(s.t, s.cpu, s.gpu, s.fan));
            if (control) QTimer::singleShot(0, &fanState, [&] { if (!fans.quitting()) fanState.restore(); });
        }
        if (alerts) alerts->update();
        fansProps.publish();
    });
    api.extraStatus = [&] {
        return QVariantMap{ { u"nbfcPollMs"_s, nbfc.monitor().interval() }, { u"sensorsActive"_s, sensors.active() },
                            { u"sensorsFast"_s, sensors.fast() } };
    };
    pace();
    sensors.start();                              // only now: a second copy that exits with 3 reads nothing
    nbfc.start();
    keyboard.start();

    std::fprintf(stderr, "solwindd %s started, %s mode (settings: %s%s)\n", SOLWIND_VERSION, control ? "control" : "observe",
                 qPrintable(settings.path()), loaded ? "" : ", not read: defaults");
    const int rc = app.exec();
    settings.flush();
    if (bus.isConnected()) bus.unregisterService(name);
    return rc;
}
