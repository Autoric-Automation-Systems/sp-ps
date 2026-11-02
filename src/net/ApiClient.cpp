#include "spps/net/ApiClient.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "spps/Logger.h"

namespace spps
{
namespace
{

bool isSuccess(int status)
{
  return status >= 200 && status < 300;
}

uint32_t readIntervalHint(const JsonVariant &value)
{
  // O servidor devolve número ou string (ex.: "600000").
  if (value.is<const char *>())
  {
    return static_cast<uint32_t>(strtoul(value.as<const char *>(), nullptr, 10));
  }
  if (value.isNull())
  {
    return 0;
  }
  return static_cast<uint32_t>(value.as<double>());
}

// Copia texto do JSON para um buffer fixo, tolerando campo ausente ou de outro tipo.
void copyText(const JsonVariant &value, char *destination, size_t size)
{
  destination[0] = '\0';
  if (size < 2 || !value.is<const char *>())
  {
    return;
  }
  strncpy(destination, value.as<const char *>(), size - 1);
  destination[size - 1] = '\0';
}

} // namespace

ApiClient::ApiClient(ITransport &transport, const char *baseUrl) : transport_(transport)
{
  snprintf(heartbeatUrl_, sizeof(heartbeatUrl_), "%s/api/devices/heartbeat", baseUrl);
  snprintf(eventsUrl_, sizeof(eventsUrl_), "%s/api/devices/events", baseUrl);
}

bool ApiClient::postJson(const char *url, const char *body)
{
  const int status = transport_.post(url, body, response_, sizeof(response_));
  log::info("POST %s -> %d", url, status);

  if (!isSuccess(status))
  {
    return false;
  }
  if (response_[0] != '\0')
  {
    log::info("%s", response_);
  }
  return true;
}

bool ApiClient::sendHeartbeat(const HeartbeatStatus &status, HeartbeatReply &reply)
{
  reply.intervalMs = 0;
  reply.counterInterval = 0;
  reply.firmwareVersion[0] = '\0';
  reply.firmwareUrl[0] = '\0';

  StaticJsonDocument<384> document;
  document["mac"] = status.mac;
  document["type"] = status.deviceType;
  document["version"] = status.version;
  document["wifi"] = status.rssi;
  document["ip"] = status.ip;
  document["uptime"] = status.uptimeMs;
  document["boots"] = status.boots;
  document["heap"] = status.freeHeap;
  serializeJson(document, body_, sizeof(body_));
  log::info("%s", body_);

  if (!postJson(heartbeatUrl_, body_))
  {
    return false;
  }

  StaticJsonDocument<256> response;
  if (deserializeJson(response, response_) != DeserializationError::Ok)
  {
    log::info("Resposta de heartbeat sem JSON válido");
    return true;
  }

  reply.intervalMs = readIntervalHint(response["heartbeatInterval"]);
  reply.counterInterval = readIntervalHint(response["counterInterval"]);
  copyText(response["firmwareVersion"], reply.firmwareVersion, sizeof(reply.firmwareVersion));
  copyText(response["firmwareUrl"], reply.firmwareUrl, sizeof(reply.firmwareUrl));
  return true;
}

size_t ApiClient::sendEvents(const char *mac, const Event *events, size_t count,
                             size_t batchSize, uint32_t nowEpoch)
{
  size_t delivered = 0;
  if (count == 0 || batchSize == 0)
  {
    return 0;
  }

  while (delivered < count)
  {
    const size_t batch = (count - delivered < batchSize) ? count - delivered : batchSize;
    const size_t capacity = JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(batch) +
                            batch * JSON_OBJECT_SIZE(3) + 64;

    // Os valores precisam ir com 2 casas: serializar o float direto gera
    // 29.63999939. O texto formatado vive até o serializeJson abaixo.
    char valueText[kMaxEventsPerBatch][16];

    DynamicJsonDocument document(capacity);
    document["mac"] = mac;
    JsonArray payload = document.createNestedArray("events");
    for (size_t i = 0; i < batch; i++)
    {
      const Event &event = events[delivered + i];
      formatEventValue(event.value, valueText[i], sizeof(valueText[i]));

      JsonObject item = payload.createNestedObject();
      item["name"] = event.name;
      item["value"] = serialized(valueText[i]);
      item["ts"] = event.ts != 0 ? event.ts : nowEpoch;
    }

    serializeJson(document, body_, sizeof(body_));
    log::info("%s", body_);

    if (!postJson(eventsUrl_, body_))
    {
      break;
    }
    delivered += batch;
  }

  return delivered;
}

} // namespace spps
