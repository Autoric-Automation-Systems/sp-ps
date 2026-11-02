#include "spps/net/SystemClock.h"

#include <Arduino.h>
#include <time.h>

#include "spps/Logger.h"

namespace spps
{
namespace
{

constexpr uint32_t kSyncPollIntervalMs = 500;

bool isTimeValid(time_t value)
{
  return value >= static_cast<time_t>(SystemClock::kMinValidEpoch);
}

} // namespace

SystemClock::SystemClock(const char *ntpServer, long gmtOffsetSeconds, int daylightOffsetSeconds)
    : ntpServer_(ntpServer),
      gmtOffsetSeconds_(gmtOffsetSeconds),
      daylightOffsetSeconds_(daylightOffsetSeconds)
{
}

uint32_t SystemClock::epochNow()
{
  const time_t now = time(nullptr);
  if (isTimeValid(now))
  {
    lastValidEpoch_ = static_cast<uint32_t>(now);
    lastValidMillis_ = millis();
    return lastValidEpoch_;
  }

  if (lastValidEpoch_ != 0)
  {
    return lastValidEpoch_ + (millis() - lastValidMillis_) / 1000UL;
  }
  return 0;
}

bool SystemClock::ensureSynced(uint32_t timeoutMs)
{
  if (isTimeValid(time(nullptr)))
  {
    return true;
  }

  configTime(gmtOffsetSeconds_, daylightOffsetSeconds_, ntpServer_);
  log::info("Sincronizando NTP...");

  const uint32_t start = millis();
  while (!isTimeValid(time(nullptr)) && millis() - start < timeoutMs)
  {
    delay(kSyncPollIntervalMs);
  }

  const time_t now = time(nullptr);
  if (!isTimeValid(now))
  {
    log::info("NTP indisponível: eventos serão carimbados no envio");
    return false;
  }

  lastValidEpoch_ = static_cast<uint32_t>(now);
  lastValidMillis_ = millis();
  log::info("NTP OK: %s", ctime(&now));
  return true;
}

} // namespace spps
