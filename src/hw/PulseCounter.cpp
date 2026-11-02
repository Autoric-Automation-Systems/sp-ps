#include "spps/hw/PulseCounter.h"

#include <Arduino.h>

namespace spps
{

volatile uint32_t PulseCounter::pulseCount_ = 0;
uint32_t PulseCounter::debounceMs_ = 10;
volatile uint32_t PulseCounter::lastPulseMillis_ = 0;

PulseCounter::PulseCounter(uint8_t pin, uint32_t debounceMs) : pin_(pin)
{
  debounceMs_ = debounceMs;
}

void PulseCounter::begin()
{
  pinMode(pin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin_), handlePulse, RISING);
}

uint32_t PulseCounter::consumePulses()
{
  noInterrupts();
  const uint32_t pulses = pulseCount_;
  pulseCount_ = 0;
  interrupts();
  return pulses;
}

void IRAM_ATTR PulseCounter::handlePulse()
{
  // Pulsos mais próximos que o debounce são ruído elétrico, não contagem.
  const uint32_t now = millis();
  if (now - lastPulseMillis_ <= debounceMs_)
  {
    return;
  }
  lastPulseMillis_ = now;
  pulseCount_ = pulseCount_ + 1;
}

} // namespace spps
