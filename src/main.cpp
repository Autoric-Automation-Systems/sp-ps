#include <Arduino.h>

#include "spps/Config.h"
#include "spps/app/Device.h"
#include "spps/app/SettingsStore.h"
#include "spps/core/EventQueue.h"
#include "spps/hw/DigitalInputs.h"
#include "spps/hw/PersistentStorage.h"
#include "spps/hw/PulseCounter.h"
#include "spps/core/SignalMonitor.h"
#include "spps/net/ApiClient.h"
#include "spps/net/HttpOtaUpdater.h"
#include "spps/net/SecureHttpTransport.h"
#include "spps/net/SystemClock.h"
#include "spps/net/WifiLink.h"
#include "spps/power/RuntimeController.h"

namespace
{

using namespace spps;

// Composition root: o único lugar que conhece todas as implementações.
SystemClock g_clock("pool.ntp.org", -3 * 3600, 0);
WifiLink g_network(config::kPortalSsid);
SecureHttpTransport g_transport;
ApiClient g_api(g_transport, config::kBaseUrl);
EventQueue g_queue(persistentQueueStorage());
PulseCounter g_counter(config::kCounterPin, config::kCounterDebounceMs);
DigitalInputs g_inputs(config::kEventPin1, config::kEventPin2, config::kEventPin3);
SignalMonitor g_signal(config::kRssiReportDeltaDbm);
SettingsStore g_settings(config::kHeartbeatLimits, config::kCounterLimits, "spps");
HttpOtaUpdater g_updater;
RuntimeController g_power;

Device g_device(Device::Dependencies{g_clock, g_network, g_api, g_queue, g_counter,
                                     g_inputs, g_signal, g_settings, g_updater,
                                     g_power, persistentDeviceState()});

} // namespace

void setup()
{
  g_device.begin();
}

void loop()
{
  g_device.tick();
}
