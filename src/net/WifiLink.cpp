#include "spps/net/WifiLink.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <cstdio>
#include <esp_mac.h>

#include "spps/Logger.h"

namespace spps
{
namespace
{

constexpr uint16_t kMillisPerRetry = 500;
constexpr uint16_t kPortalConnectTimeoutSeconds = 20;

void applyTimeouts(WiFiManager &manager, uint16_t portalTimeoutSeconds)
{
  manager.setConfigPortalTimeout(portalTimeoutSeconds);
  manager.setConnectTimeout(kPortalConnectTimeoutSeconds);
}

} // namespace

bool WifiLink::autoConnect(uint16_t portalTimeoutSeconds)
{
  WiFiManager manager;
  applyTimeouts(manager, portalTimeoutSeconds);

  if (!manager.autoConnect(apName_))
  {
    log::info("Sem WiFi no boot, seguindo sem rede (tenta a cada heartbeat)");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  log::info("WiFi conectado");
  return true;
}

bool WifiLink::connect(uint16_t retries)
{
  if (connected())
  {
    return true;
  }

  // Sem disconnect(true) antes, o stack do WiFi pode ficar preso ao religar
  // o rádio depois de um WIFI_OFF.
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  for (uint16_t attempt = 0; attempt < retries && !connected(); attempt++)
  {
    delay(kMillisPerRetry);
  }

  if (!connected())
  {
    log::info("Falha ao conectar no WiFi");
    return false;
  }

  log::info("WiFi conectado (%d dBm)", rssi());
  return true;
}

void WifiLink::disconnect()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool WifiLink::connected()
{
  return WiFi.status() == WL_CONNECTED;
}

int WifiLink::rssi()
{
  return WiFi.RSSI();
}

const char *WifiLink::localIp()
{
  const IPAddress address = WiFi.localIP();
  snprintf(ip_, sizeof(ip_), "%u.%u.%u.%u",
           static_cast<unsigned>(address[0]), static_cast<unsigned>(address[1]),
           static_cast<unsigned>(address[2]), static_cast<unsigned>(address[3]));
  return ip_;
}

const char *WifiLink::macAddress()
{
  // Lido do eFuse: não depende de o driver WiFi estar iniciado (é a mesma
  // identidade que o servidor já conhece, em maiúsculas com dois-pontos).
  if (mac_[0] == '\0')
  {
    uint8_t raw[6] = {0};
    if (esp_read_mac(raw, ESP_MAC_WIFI_STA) == ESP_OK)
    {
      snprintf(mac_, sizeof(mac_), "%02X:%02X:%02X:%02X:%02X:%02X",
               raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
    }
  }
  return mac_;
}

void WifiLink::openPortal(uint16_t timeoutSeconds)
{
  log::info("Abrindo portal de configuração por %u s", timeoutSeconds);

  WiFiManager manager;
  applyTimeouts(manager, timeoutSeconds);
  manager.startConfigPortal(apName_);

  log::info("Portal encerrado");
  disconnect();
}

} // namespace spps
