#include "spps/net/HttpOtaUpdater.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
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

bool HttpOtaUpdater::install(const char *url)
{
  if (url == nullptr || url[0] == '\0')
  {
    log::info("OTA sem URL, ignorado");
    return false;
  }

  log::info("OTA: baixando %s", url);

  // HTTP puro não pode usar WiFiClientSecure: ele sempre faz handshake TLS e um
  // servidor http:// (bancada) recusaria a conexão.
  const bool isHttps = strncmp(url, "https", 5) == 0;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  NetworkClient *client = &plainClient;
  if (isHttps)
  {
    configureTls(secureClient);
    client = &secureClient;
  }

  // O HTTPUpdate não expõe timeouts: o HTTPClient é configurado aqui e o
  // download acontece pela sobrecarga que recebe um HTTPClient já iniciado.
  HTTPClient http;
  http.setConnectTimeout(static_cast<int32_t>(config::kOtaConnectTimeoutMs));
  http.setTimeout(static_cast<uint16_t>(config::kOtaReadTimeoutMs));
  if (!http.begin(*client, url))
  {
    log::info("OTA: não foi possível iniciar a requisição");
    return false;
  }

  httpUpdate.rebootOnUpdate(false); // o reboot é decidido pelo orquestrador
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  const t_httpUpdate_return result = httpUpdate.update(http);
  http.end();

  switch (result)
  {
  case HTTP_UPDATE_OK:
    log::info("OTA: imagem gravada e validada na partição inativa");
    return true;
  case HTTP_UPDATE_NO_UPDATES:
    log::info("OTA: servidor sem atualização");
    return false;
  case HTTP_UPDATE_FAILED:
  default:
    log::info("OTA falhou (%d): %s", httpUpdate.getLastError(),
              httpUpdate.getLastErrorString().c_str());
    return false;
  }
}

} // namespace spps
