#include "spps/hw/DigitalInputs.h"

#include <Arduino.h>

namespace spps
{

DigitalInputs::DigitalInputs(uint8_t pin1, uint8_t pin2, uint8_t pin3)
    : pins_{pin1, pin2, pin3}, last_{false, false, false}
{
}

void DigitalInputs::begin()
{
  for (uint8_t i = 0; i < kCount; i++)
  {
    pinMode(pins_[i], INPUT_PULLUP);
    // Ativo em nível baixo (contato para GND): inverte a leitura.
    last_[i] = !digitalRead(pins_[i]);
  }
}

uint8_t DigitalInputs::poll(InputChange *changes, uint8_t maxChanges)
{
  uint8_t count = 0;
  for (uint8_t i = 0; i < kCount; i++)
  {
    const bool current = !digitalRead(pins_[i]);
    if (current == last_[i])
    {
      continue;
    }
    last_[i] = current;
    if (count < maxChanges)
    {
      changes[count].index = static_cast<uint8_t>(i + 1);
      changes[count].active = current;
      count++;
    }
  }
  return count;
}

} // namespace spps
