#include "spps/net/SecureHttpTransport.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>

#include "spps/Config.h"
#include "spps/Logger.h"
#include "spps/roots_ca.h"

namespace spps
{
namespace
{

void configureTls(WiFiClientSecure &client)
{
#if TLS_VERIFY
  client.setCACert(SMARTPLANT_CA_BUNDLE);
#else
  client.setInsecure();
#endif
}

} // namespace

int SecureHttpTransport::post(const char *url, const char *body, char *response, size_t responseSize)
{
  if (response != nullptr && responseSize > 0)
  {
    response[0] = '\0';
  }

  // HTTP puro (servidor local de desenvolvimento) não pode usar TLS: o
  // WiFiClientSecure sempre faz handshake e a conexão seria recusada.
  const bool isHttps = strncmp(url, "https", 5) == 0;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  NetworkClient *client = &plainClient;
  if (isHttps)
  {
    configureTls(secureClient);
    client = &secureClient;
  }

  HTTPClient http;
  if (!http.begin(*client, url))
  {
    log::info("Falha ao iniciar HTTP: %s", url);
    return -1;
  }

  http.setConnectTimeout(static_cast<int32_t>(config::kHttpConnectTimeoutMs));
  http.setTimeout(static_cast<uint16_t>(config::kHttpReadTimeoutMs));
  http.addHeader("Content-Type", "application/json");
  if (config::kDeviceApiKey[0] != '\0')
  {
    http.addHeader("x-api-key", config::kDeviceApiKey);
  }

  const int status = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body)), strlen(body));

  if (status > 0 && response != nullptr && responseSize > 1)
  {
    const String payload = http.getString();
    strncpy(response, payload.c_str(), responseSize - 1);
    response[responseSize - 1] = '\0';
  }

  http.end();
  return status;
}

} // namespace spps
