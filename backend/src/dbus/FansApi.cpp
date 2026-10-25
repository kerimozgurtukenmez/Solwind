#include "FansApi.h"

#include "core/History.h"
#include "core/Sensors.h"
#include "plugins/fan/nbfc/NbfcMonitor.h"

#include <QDBusConnection>
#include <QDBusMessage>

using namespace Qt::StringLiterals;

FansApi::FansApi(NbfcMonitor &nbfc, const Sensors &sensors, const SampleHistory &history, QObject *parent)
    : QObject(parent), m_nbfc(nbfc), m_sensors(sensors), m_history(history)
{
}

bool FansApi::running() const { return m_nbfc.running(); }
bool FansApi::readOnly() const { return m_nbfc.readOnly(); }
double FansApi::temp() const { return m_nbfc.temp(); }

VariantMapList FansApi::fans() const
{
    VariantMapList out;
    const auto &fans = m_nbfc.fans();
    for (int i = 0; i < fans.size(); ++i) {
        const Nbfc::FanStatus &f = fans[i];
        out.append({ { u"name"_s, f.name }, { u"temp"_s, f.temp }, { u"auto"_s, f.autoControl },
                     { u"critical"_s, f.critical }, { u"current"_s, f.current }, { u"target"_s, f.target },
                     { u"rpm"_s, m_sensors.state().rpm(i, int(fans.size())) } });
    }
    return out;
}

QString FansApi::configId() const { return m_nbfc.configId(); }
QString FansApi::fileConfigId() const { return m_nbfc.fileConfigId(); }
bool FansApi::noConfig() const { return m_nbfc.noConfig(); }
bool FansApi::configPending() const { return m_nbfc.configPending(); }
bool FansApi::bootEnabled() const { return m_nbfc.bootEnabled(); }
QStringList FansApi::configs() const { return m_nbfc.configs(); }

QVariantMap FansApi::configList() const
{
    return { { u"dirs"_s, m_nbfc.configDirsUsed() }, { u"fromDirs"_s, int(m_nbfc.configsFromDirs().size()) },
             { u"fromCli"_s, int(m_nbfc.configsFromCli().size()) }, { u"cliCode"_s, m_nbfc.cliListCode() },
             { u"cliLine"_s, m_nbfc.cliListLine() } };
}

VariantMapList FansApi::configCurves() const
{
    VariantMapList out;
    for (const auto &c : m_nbfc.configCurves()) {
        if (!c) { out.append(QVariantMap()); continue; }
        VariantMapList pts;
        for (const Nbfc::CurvePoint &p : c->points) pts.append({ { u"t"_s, p.t }, { u"s"_s, p.s } });
        out.append({ { u"points"_s, QVariant::fromValue(pts) }, { u"hyst"_s, c->hyst } });
    }
    return out;
}

QStringList FansApi::recommended() const { return m_nbfc.recommended(); }

QVariantMap FansApi::sampleMap(qint64 t, double cpu, double gpu, double fan)
{
    return { { u"t"_s, t }, { u"cpu"_s, cpu }, { u"gpu"_s, gpu }, { u"fan"_s, fan } };
}

bool FansApi::Recommend(QStringList &)
{
    // Answered when nbfc config -r has finished. It waits in the command queue behind the other nbfc commands (each
    // limited to 15 s), so a client should allow more than D-Bus's default 25 s.
    if (!calledFromDBus()) return false;
    setDelayedReply(true);
    const QDBusMessage msg = message();
    QDBusConnection conn = connection();
    m_nbfc.recommend([msg, conn](bool ok, const QStringList &found) mutable {
        conn.send(msg.createReply(QVariantList{ ok, found }));
    });
    return false;
}

VariantMapList FansApi::History() const
{
    VariantMapList out;
    for (const auto &s : m_history.samples()) out.append(sampleMap(s.t, s.cpu, s.gpu, s.fan));
    return out;
}
