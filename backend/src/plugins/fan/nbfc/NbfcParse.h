#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <limits>
#include <optional>

// Pure functions of services/Nbfc.qml, ported rule by rule (SPEC.md N-1..N-8, A-10..A-12, Q-5) and checked against
// the QML with .dev/phase2diff.sh.
namespace Nbfc {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

struct FanStatus {
    QString name;
    double temp = NaN;
    bool autoControl = true;
    bool critical = false;                // "Critical Mode Enabled": NBFC runs it at 100%
    double current = 0;
    double target = 0;
};

struct Status {
    std::optional<bool> readOnly;         // only if the output said
    double temp = NaN;                    // the service's temperature (N-8 fills it from the fans if missing)
    QList<FanStatus> fans;
    QString config;                       // "Selected Config Name" ("" if not reported)
};

struct CurvePoint {
    int t = 0;
    int s = 0;
    bool operator==(const CurvePoint &) const = default;
};
struct ConfigCurve {
    QList<CurvePoint> points;
    int hyst = 3;
    bool operator==(const ConfigCurve &) const = default;
};

// The first line that says something: nbfc_service starts every run with "INFO: ... comes with no warranty"
QString firstLine(const QString &s);
// Non-empty trimmed lines
QStringList lines(const QString &s);
// true|yes|on|enabled|1 at the start, any case
bool isTrue(const QString &v);

// `nbfc status -a` (N-1..N-7), then N-8's temperature rule
Status parseStatus(const QString &txt);

// One fan's TemperatureThresholds as a curve (A-12); nullopt = no usable table. `thrown` is set where the QML
// threw a TypeError (a null row): the caller then has no curves at all, as the QML's try/catch gave [].
std::optional<ConfigCurve> toCurve(const QJsonValue &thresholds, bool *thrown);
// The config file's text -> one entry per FanConfigurations item (nullopt = no table); [] if it can't be used
QList<std::optional<ConfigCurve>> configCurves(const QByteArray &fileText);

// The config list (A-10): the folders' list if not empty, else `nbfc config -l`'s; the selected ones always in;
// de-duplicated, sorted the way the QML did (localeCompare, QV4's sort)
QStringList mergeConfigs(const QStringList &fromDirs, const QStringList &fromCli, const QString &configId,
                         const QString &fileConfigId);
// /etc/nbfc/nbfc.json's text -> JSON.parse(text).SelectedConfigId || "" as a QML string property takes it;
// nullopt where the QML threw (not JSON, or null): the current id is kept then. `why` tells which.
std::optional<QString> selectedConfigId(const QByteArray &text, QString *why = nullptr);
// `nbfc config -l 2>&1`: its lines without usage/error/warning ones
QStringList parseConfigList(const QString &out);
// `nbfc config -r` (A-11): the known configs it names, in its order
QStringList matchRecommendations(const QString &out, const QStringList &known);

} // namespace Nbfc
