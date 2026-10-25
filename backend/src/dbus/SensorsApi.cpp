#include "SensorsApi.h"

#include "core/Sensors.h"

using namespace Qt::StringLiterals;

SensorsApi::SensorsApi(const Sensors &s, QObject *parent) : QObject(parent), m_s(s) {}

VariantMapList SensorsApi::readings() const
{
    VariantMapList out;
    for (const Reading &r : m_s.state().readings)
        out.append({ { u"label"_s, r.label }, { u"model"_s, r.model }, { u"threads"_s, r.threads },
                     { u"value"_s, r.value }, { u"note"_s, r.note } });
    return out;
}

QVariantMap SensorsApi::hardware() const
{
    const HwNames &h = m_s.state().hw;
    return { { u"cpu"_s, h.cpu }, { u"cores"_s, h.cores }, { u"gpu"_s, h.gpu }, { u"igpu"_s, h.igpu } };
}

double SensorsApi::cpu() const { return m_s.state().cpu(); }
double SensorsApi::gpu() const { return m_s.state().gpu(); }
bool SensorsApi::hasGpu() const { return m_s.state().hasGpu(); }
DoubleList SensorsApi::rpms() const { return m_s.state().rpms; }
bool SensorsApi::rpmWorks() const { return m_s.state().rpmWorks; }
