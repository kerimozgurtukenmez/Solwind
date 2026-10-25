#pragma once

#include "Paths.h"

#include <QString>

#include <atomic>

// Reads the sensors the way scripts/sensors.sh and scripts/hwinfo.sh did, and returns the same text, line for line
// (SPEC.md S-1..S-3), so the parser (Sensors::parse) works on exactly what the old app parsed. Blocking file reads:
// meant for a worker thread (on some laptops a fan*_input read is an EC call of ~35 ms).
// Only reads; the one program it may start per round is nvidia-smi, and only when the card is awake.
namespace SensorReader {

struct Options {
    int smiTimeoutMs = 5000;   // nvidia-smi: SIGTERM after this (timeout -k 2 5) ...
    int smiKillMs = 2000;      // ... and SIGKILL this much later
    QString nvidiaSmi;         // "" = look it up in PATH (command -v nvidia-smi)
    const std::atomic<bool> *stop = nullptr;   // set: a running program is killed now (shutdown)
};

// One round of sensors.sh [gpu]:
//   name|label|millidegrees   for coretemp k10temp zenpower acpitz i915 xe amdgpu nouveau
//   fan|driver|number|rpm     every hwmon's fan*_input
//   nvidia||<millideg>|asleep|skip|fail   per NVIDIA display device (nvidia-smi only on gpu rounds, card awake)
QString round(const Paths &paths, bool gpu, const Options &opt = {});

// hwinfo.sh (once):  cpu|model|threads   and   gpu|vendor|pci-address|model  per display device
QString hwInfo(const Paths &paths, const std::atomic<bool> *stop = nullptr);

} // namespace SensorReader
