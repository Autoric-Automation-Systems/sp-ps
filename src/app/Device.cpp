#include "spps/app/Device.h"

#include <Arduino.h>
#include <esp_system.h>

#include "spps/Config.h"
#include "spps/Logger.h"
#include "spps/core/Event.h"
#include "spps/core/FirmwareVersion.h"

namespace spps
{
namespace
{

constexpr const char *kCounterEvent = "counter";
constexpr const char *kEventNames[DigitalInputs::kCount] = {"event1", "event2", "event3"};
constexpr const char *kWifiEvent = "wifi";

const char *resetReasonName(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_EXT:
    return "EXTERNAL";
  case ESP_RST_SW:
    return "SOFTWARE";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WDT";
  case ESP_RST_TASK_WDT:
    return "TASK_WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  default:
    return "UNKNOWN";
  }
}

Event makeEvent(const char *name, float value, uint32_t ts)
{
  Event event{};
  setEventName(event, name);
  event.value = roundEventValue(value);
  event.ts = ts;
  return event;
}

} // namespace

Device::Device(const Dependencies &dependencies)
    : dependencies_(dependencies),
      health_(config::kPortalAfterWifiFailures, config::kRestartAfterSendFailures)
{
}

void Device::begin()
{
  beginPersistentState();
  dependencies_.queue.restore();
  log::begin(115200);

  log::blank();
  log::info("Boot Smart Plant %s | %s | boot #%lu | reset: %s | slot: %s",
            config::kFirmwareVersion, config::kDeviceType,
            static_cast<unsigned long>(dependencies_.state.bootCount),
            resetReasonName(esp_reset_reason()), dependencies_.power.firmwareSlot());

  // NVS só é acessada depois do boot do Arduino (nunca em construtor global).
  loadSettings();

  mac_ = dependencies_.network.macAddress();
  dependencies_.signal.seed(dependencies_.state.lastRssi);
  log::info("MAC: %s | heartbeat: %lu ms | counterInterval: %lu | fila: %u evento(s)",
            mac_, static_cast<unsigned long>(timer_.period()),
            static_cast<unsigned long>(counterInterval_),
            static_cast<unsigned>(dependencies_.queue.size()));

  dependencies_.counter.begin();
  dependencies_.inputs.begin();
  dependencies_.network.autoConnect(config::kPortalTimeoutSeconds);
  dependencies_.clock.ensureSynced(config::kNtpTimeoutMs);

  timer_.start(millis());
}

void Device::loadSettings()
{
  const uint32_t heartbeat = sanitizeInterval(dependencies_.settings.heartbeatInterval(), config::kHeartbeatLimits);
  timer_.setPeriod(heartbeat);
  counterInterval_ = sanitizeInterval(dependencies_.settings.counterInterval(), config::kCounterLimits);
}

void Device::tick()
{
  publishCounterEvents();
  publishInputEvents();

  if (!timer_.due(millis()))
  {
    return;
  }

  timer_.scheduleNext(millis());
  runNetworkCycle();

  if (health_.consumeReconfigurationRequest())
  {
    dependencies_.network.openPortal(config::kPortalTimeoutSeconds);
  }

  if (health_.requiresRestart())
  {
    log::info("%u ciclos seguidos sem envio, reiniciando...",
              static_cast<unsigned>(health_.sendFailures()));
    dependencies_.power.restart();
  }
}

void Device::runNetworkCycle()
{
  const bool connected = dependencies_.network.connect(config::kWifiConnectRetries);
  health_.recordWifi(connected);

  bool heartbeatOk = false;
  if (connected)
  {
    dependencies_.clock.ensureSynced(config::kNtpTimeoutMs);
    publishSignalEvent();
    publishQueuedEvents();

    HeartbeatReply reply{};
    heartbeatOk = sendHeartbeat(reply);
    if (heartbeatOk && applyFirmwareUpdate(reply))
    {
      // A imagem já está validada na partição inativa: o reboot a ativa.
      dependencies_.power.restart();
      return;
    }

    dependencies_.network.disconnect();
  }
  else
  {
    log::info("Sem WiFi (%u ciclos seguidos), nada enviado",
              static_cast<unsigned>(health_.wifiFailures()));
  }

  health_.recordSend(heartbeatOk);
  reportStatus();
}

void Device::publishCounterEvents()
{
  // Os pulsos que sobram da divisão ficam no estado persistido: um restart no
  // meio da contagem não perde o que já foi medido.
  uint32_t done = dependencies_.state.pulsesPending / counterInterval_;
  uint32_t rest = dependencies_.state.pulsesPending % counterInterval_;

  const uint32_t pulses = dependencies_.counter.consumePulses();
  if (pulses > 0)
  {
    log::info("Pulsos: %lu", static_cast<unsigned long>(pulses));
  }

  const uint32_t total = rest + pulses;
  done += total / counterInterval_;
  rest = total % counterInterval_;

  dependencies_.state.pulsesPending = rest;

  for (uint32_t i = 0; i < done; i++)
  {
    if (!dependencies_.queue.push(
            makeEvent(kCounterEvent, static_cast<float>(counterInterval_), dependencies_.clock.epochNow())))
    {
      log::info("Fila cheia: evento mais antigo descartado");
    }
  }
}

void Device::publishInputEvents()
{
  InputChange changes[DigitalInputs::kCount];
  const uint8_t count = dependencies_.inputs.poll(changes, DigitalInputs::kCount);

  for (uint8_t i = 0; i < count; i++)
  {
    const uint8_t index = changes[i].index;
    if (index == 0 || index > DigitalInputs::kCount)
    {
      continue;
    }
    dependencies_.queue.push(
        makeEvent(kEventNames[index - 1], changes[i].active ? 1.0f : 0.0f, dependencies_.clock.epochNow()));
  }
}

void Device::publishSignalEvent()
{
  const int rssi = dependencies_.network.rssi();
  if (!SignalMonitor::isValid(rssi))
  {
    log::info("RSSI inválido: %d", rssi);
    return;
  }
  if (!dependencies_.signal.shouldReport(rssi))
  {
    return;
  }

  dependencies_.state.lastRssi = rssi;
  dependencies_.queue.push(makeEvent(kWifiEvent, static_cast<float>(rssi), dependencies_.clock.epochNow()));
}

void Device::publishQueuedEvents()
{
  if (dependencies_.queue.empty())
  {
    return;
  }

  const uint32_t now = dependencies_.clock.epochNow();
  if (now == 0)
  {
    log::info("Sem hora válida: eventos continuam na fila");
    return;
  }

  const size_t delivered = dependencies_.api.sendEvents(
      mac_, dependencies_.queue.data(), dependencies_.queue.size(),
      config::kEventsPerBatch, now);

  if (delivered > 0)
  {
    dependencies_.queue.dropFront(delivered);
  }
  if (!dependencies_.queue.empty())
  {
    log::info("Envio incompleto: %u evento(s) mantidos na fila",
              static_cast<unsigned>(dependencies_.queue.size()));
  }
}

bool Device::sendHeartbeat(HeartbeatReply &reply)
{
  HeartbeatStatus status{};
  status.mac = mac_;
  status.deviceType = config::kDeviceType;
  status.version = config::kFirmwareVersion;
  status.rssi = dependencies_.network.rssi();
  status.ip = dependencies_.network.localIp();
  status.uptimeMs = millis();
  status.boots = dependencies_.state.bootCount;
  status.freeHeap = ESP.getFreeHeap();

  const bool ok = dependencies_.api.sendHeartbeat(status, reply);
  applyIntervalHints(reply);
  return ok;
}

bool Device::applyFirmwareUpdate(const HeartbeatReply &reply)
{
  const char *url = reply.firmwareUrl;
  bool skipVersionCheck = false;

  // Atalho de bancada: força o download sem depender da resposta do servidor.
  if (config::kForcedUpdateUrl[0] != '\0')
  {
    url = config::kForcedUpdateUrl;
    skipVersionCheck = true;
  }

  if (url == nullptr || url[0] == '\0')
  {
    return false;
  }

  if (!skipVersionCheck && !isNewerVersion(reply.firmwareVersion, config::kFirmwareVersion))
  {
    log::info("OTA: versão %s não é mais nova que %s", reply.firmwareVersion,
              config::kFirmwareVersion);
    return false;
  }

  log::info("OTA: %s -> %s", config::kFirmwareVersion,
            skipVersionCheck ? "forçada (bancada)" : reply.firmwareVersion);
  return dependencies_.updater.install(url);
}

void Device::applyIntervalHints(const HeartbeatReply &reply)
{
  if (reply.intervalMs != 0)
  {
    if (!isIntervalAllowed(reply.intervalMs, config::kHeartbeatLimits))
    {
      log::info("heartbeatInterval fora da faixa, ignorado: %lu",
                static_cast<unsigned long>(reply.intervalMs));
    }
    else if (reply.intervalMs != timer_.period())
    {
      timer_.setPeriod(reply.intervalMs);
      dependencies_.settings.saveHeartbeatInterval(reply.intervalMs);
      timer_.scheduleNext(millis());
      log::info("Novo intervalo de heartbeat: %lu ms", static_cast<unsigned long>(reply.intervalMs));
    }
    else
    {
      log::info("heartbeatInterval sem alteração");
    }
  }

  if (reply.counterInterval != 0)
  {
    if (!isIntervalAllowed(reply.counterInterval, config::kCounterLimits))
    {
      log::info("counterInterval fora da faixa, ignorado: %lu",
                static_cast<unsigned long>(reply.counterInterval));
    }
    else if (reply.counterInterval != counterInterval_)
    {
      counterInterval_ = reply.counterInterval;
      dependencies_.settings.saveCounterInterval(counterInterval_);
      log::info("Novo counterInterval: %lu", static_cast<unsigned long>(counterInterval_));
    }
    else
    {
      log::info("counterInterval sem alteração");
    }
  }
}

void Device::reportStatus()
{
  log::info("Heap livre: %lu | fila: %u evento(s) | descartados: %lu | pendentes: %lu",
            static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned>(dependencies_.queue.size()),
            static_cast<unsigned long>(dependencies_.queue.dropped()),
            static_cast<unsigned long>(dependencies_.state.pulsesPending));
}

} // namespace spps
