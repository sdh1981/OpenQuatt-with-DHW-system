#pragma once

// Telt gebeurtenissen over een voortschrijdend uur. Bewust vrij van ESPHome:
// alleen <array>/<cstdint>, zodat het host-testbaar is
// (tests/host/oq_rolling_hour_counter_test.cpp).
//
// Niet elke gebeurtenis apart bewaren, zoals CompressorStartLimit met zijn zes
// tijdstempels doet: standwissels kunnen er op een ritme van 10 s honderden per
// uur zijn. Zes emmers van 10 minuten kosten 12 bytes en zijn nauwkeurig genoeg
// voor een teller waar je een ritmekeuze op baseert.
//
// Gevolg van die keuze: het venster is niet exact een uur maar loopt mee met de
// emmers, dus tussen 50 en 60 minuten. Dat is de prijs van de resolutie.

#include <array>
#include <cstdint>

namespace oq_rate {

class RollingHourCounter {
 public:
  static constexpr uint32_t BUCKET_MS = 10U * 60U * 1000U;
  static constexpr uint8_t BUCKETS = 6U;  // 6 x 10 min

  void record(uint32_t now_ms) {
    this->advance_(now_ms);
    if (this->buckets_[this->head_] < UINT16_MAX) ++this->buckets_[this->head_];
  }

  uint16_t count(uint32_t now_ms) {
    this->advance_(now_ms);
    uint32_t total = 0;
    for (uint8_t i = 0; i < BUCKETS; ++i) total += this->buckets_[i];
    return total > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(total);
  }

  void reset() {
    this->buckets_ = {};
    this->head_ = 0;
    this->started_ = false;
    this->head_started_ms_ = 0;
  }

 private:
  // Schuift het venster op. Verschillen worden unsigned gerekend, zodat een
  // millis()-omslag geen sprong van 49 dagen oplevert.
  void advance_(uint32_t now_ms) {
    if (!this->started_) {
      this->started_ = true;
      this->head_started_ms_ = now_ms;
      return;
    }
    const uint32_t elapsed_ms = static_cast<uint32_t>(now_ms - this->head_started_ms_);
    if (elapsed_ms < BUCKET_MS) return;
    const uint32_t steps = elapsed_ms / BUCKET_MS;
    if (steps >= BUCKETS) {
      // Langer stil dan het hele venster: alles is verlopen.
      this->buckets_ = {};
      this->head_ = 0;
      this->head_started_ms_ = now_ms;
      return;
    }
    for (uint32_t i = 0; i < steps; ++i) {
      this->head_ = static_cast<uint8_t>((this->head_ + 1U) % BUCKETS);
      this->buckets_[this->head_] = 0;
    }
    this->head_started_ms_ = static_cast<uint32_t>(this->head_started_ms_ + steps * BUCKET_MS);
  }

  std::array<uint16_t, BUCKETS> buckets_{};
  uint32_t head_started_ms_{0};
  uint8_t head_{0};
  bool started_{false};
};

}  // namespace oq_rate
