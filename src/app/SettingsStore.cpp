#include "spps/app/SettingsStore.h"

namespace spps
{
namespace
{

constexpr const char *kHeartbeatKey = "hb_ms";
constexpr const char *kCounterKey = "cnt_iv";

} // namespace

SettingsStore::SettingsStore(const IntervalLimits &heartbeatLimits, const IntervalLimits &counterLimits,
                             const char *nameSpace)
    : heartbeatLimits_(heartbeatLimits), counterLimits_(counterLimits), nameSpace_(nameSpace)
{
}

uint32_t SettingsStore::heartbeatInterval()
{
  preferences_.begin(nameSpace_, true);
  const uint32_t stored = preferences_.getUInt(kHeartbeatKey, heartbeatLimits_.fallbackMs);
  preferences_.end();
  return sanitizeInterval(stored, heartbeatLimits_);
}

void SettingsStore::saveHeartbeatInterval(uint32_t valueMs)
{
  preferences_.begin(nameSpace_, false);
  preferences_.putUInt(kHeartbeatKey, sanitizeInterval(valueMs, heartbeatLimits_));
  preferences_.end();
}

uint32_t SettingsStore::counterInterval()
{
  preferences_.begin(nameSpace_, true);
  const uint32_t stored = preferences_.getUInt(kCounterKey, counterLimits_.fallbackMs);
  preferences_.end();
  return sanitizeInterval(stored, counterLimits_);
}

void SettingsStore::saveCounterInterval(uint32_t value)
{
  preferences_.begin(nameSpace_, false);
  preferences_.putUInt(kCounterKey, sanitizeInterval(value, counterLimits_));
  preferences_.end();
}

} // namespace spps
