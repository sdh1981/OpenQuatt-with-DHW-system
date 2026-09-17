#pragma once

// Overgenomen van upstream OpenQuatt v0.50.0 (PR #641), ongewijzigd op de
// namespace-plaats na. Bewust vrij van ESPHome-afhankelijkheden: alleen
// <array>/<cstdint>, zodat het een upgrade overleeft en host-testbaar is
// (tests/host/compressor_start_limit_test.cpp).

#include <array>
#include <cstdint>

namespace oq_thermal_actuator {

// Command history, not measured ODU starts. Owned by the actuator's main-loop
// runtime; deliberately not persisted. Expire on every tick, including idle
// ticks, so old timestamps cannot reappear after a complete millis() wrap.
class CompressorStartLimit {
 public:
  static constexpr uint32_t WINDOW_MS = 60U * 60U * 1000U;
  static constexpr uint8_t MAX_STARTS = 6U;

  void expire(uint32_t now_ms) {
    while (count_ > 0 && static_cast<uint32_t>(now_ms - starts_[oldest_]) >= WINDOW_MS) {
      oldest_ = (oldest_ + 1U) % MAX_STARTS;
      --count_;
    }
  }

  uint32_t remaining_ms(uint32_t now_ms) {
    expire(now_ms);
    return count_ == MAX_STARTS ? WINDOW_MS - static_cast<uint32_t>(now_ms - starts_[oldest_]) : 0U;
  }

  uint8_t count() const { return count_; }

  // Call once for the final applied transition. Rejected requests, stop writes,
  // level changes and retries of an already active command consume no slots.
  void record_transition(int previous_level, int applied_level, uint32_t now_ms) {
    expire(now_ms);
    if (previous_level != 0 || applied_level <= 0) return;
    // Defensive saturation: an unexpected extra start must not open the gate.
    if (count_ == MAX_STARTS) {
      oldest_ = (oldest_ + 1U) % MAX_STARTS;
      --count_;
    }
    starts_[(oldest_ + count_) % MAX_STARTS] = now_ms;
    ++count_;
  }

 private:
  std::array<uint32_t, MAX_STARTS> starts_{};
  uint8_t oldest_{0};
  uint8_t count_{0};
};

// De begrenzers per HP (index 0 = HP1, 1 = HP2). Eén plek, zodat de actuator ze
// bijhoudt en de Power House-dispatch kan zien welke HP nu niet mag starten.
inline std::array<CompressorStartLimit, 2>& shared_start_limits() {
  static std::array<CompressorStartLimit, 2> limits{};
  return limits;
}

// Retained defrost writes continue an active command. A zero command must go
// through normal start authorization, even if the ODU readback still is active.
constexpr bool may_retain_command(int previous_level) { return previous_level > 0; }

}  // namespace oq_thermal_actuator
