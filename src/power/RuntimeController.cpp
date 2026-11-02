#include "spps/power/RuntimeController.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "spps/Logger.h"

namespace spps
{

void RuntimeController::restart()
{
  log::info("Reiniciando para ativar o firmware novo");
  Serial.flush();
  ESP.restart();
}

const char *RuntimeController::firmwareSlot()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  return running != nullptr ? running->label : "?";
}

} // namespace spps
