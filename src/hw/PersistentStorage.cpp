#include "spps/hw/PersistentStorage.h"

#include <Arduino.h>
#include <cstddef>

#include "spps/core/Checksum.h"
#include "spps/core/SignalMonitor.h"

namespace spps
{
namespace
{

constexpr uint32_t kStateMagic = 0x53505053UL; // "SPPS"
constexpr uint32_t kMaxPlausibleBoots = 1000000UL;

RTC_NOINIT_ATTR DeviceState g_deviceState;
RTC_NOINIT_ATTR QueueStorage g_queueStorage;

uint32_t computeStateChecksum()
{
  return checksumOf(kChecksumSeed, &g_deviceState, offsetof(DeviceState, checksum));
}

void updateStateChecksum()
{
  g_deviceState.checksum = computeStateChecksum();
}

void resetState()
{
  g_deviceState.magic = kStateMagic;
  g_deviceState.bootCount = 0;
  g_deviceState.lastRssi = 0;
  g_deviceState.pulsesPending = 0;
  updateStateChecksum();
}

} // namespace

DeviceState &persistentDeviceState()
{
  return g_deviceState;
}

void beginPersistentState()
{
  // Sem checksum o bloco não é confiável: memória RTC pode vir indefinida
  // (primeira energização) ou de outro firmware.
  if (g_deviceState.magic != kStateMagic || g_deviceState.bootCount > kMaxPlausibleBoots ||
      g_deviceState.checksum != computeStateChecksum())
  {
    resetState();
  }

  g_deviceState.bootCount++;
  updateStateChecksum();
}

QueueStorage &persistentQueueStorage()
{
  return g_queueStorage;
}

} // namespace spps
