// Host-test voor de startbegrenzer (6 starts per voortschrijdend uur).
// Afgeleid van upstream OpenQuatt v0.50.0 tests/host/compressor_start_limit_test.cpp;
// de delen die upstream-only headers nodig hebben (decide_preflight, retained
// level snapshot) zijn weggelaten omdat deze fork die beslissing in de
// actuator-lambda neemt.
//
//   g++ -std=c++17 -Wall -Wextra -Werror tests/host/compressor_start_limit_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/oq_compressor_start_limit.h"

int main() {
  using oq_thermal_actuator::CompressorStartLimit;
  using oq_thermal_actuator::may_retain_command;
  constexpr uint32_t minute = 60000U;
  constexpr uint32_t hour = CompressorStartLimit::WINDOW_MS;
  CompressorStartLimit hp1;
  CompressorStartLimit hp2;
  static_assert(sizeof(CompressorStartLimit) <= 28U, "Start history must remain fixed and small");

  // Six admitted starts nine minutes apart, including a valid timestamp zero.
  for (uint32_t n = 0; n < 6; ++n) {
    const uint32_t now = n * 9 * minute;
    assert(hp1.remaining_ms(now) == 0);
    hp1.record_transition(0, 1, now);
    hp1.record_transition(1, 1, now + 1000);  // repeated active command / retry
    hp1.record_transition(1, 3, now + 2000);  // modulation
    hp1.record_transition(3, 0, now + 5 * minute);
  }
  assert(hp1.count() == 6);
  const uint32_t seventh = 54 * minute;
  assert(hp1.remaining_ms(seventh) == 6 * minute);
  assert(hp2.remaining_ms(seventh) == 0);  // Independent Duo quota.

  for (uint32_t now = seventh; now < hour; now += 5000) {
    hp1.record_transition(0, 0, now);  // Blocked or cancelled demand never consumes a slot.
    assert(hp1.remaining_ms(now) == hour - now);
  }
  assert(hp1.remaining_ms(hour - 1) == 1);
  assert(hp1.remaining_ms(hour) == 0);
  hp1.record_transition(0, 1, hour);
  assert(hp1.remaining_ms(hour) == 9 * minute);
  assert(hp1.remaining_ms(hour + 9 * minute) == 0);

  // A stopped command may not be revived by a defrost hold; a running one may.
  assert(!may_retain_command(0));
  assert(may_retain_command(1));

  // A long idle period is expired on ticks, before any complete millis wrap.
  hp1.expire(2 * hour);
  assert(hp1.remaining_ms(seventh) == 0);
  // No persistence by design: a new runtime starts a fresh history.
  CompressorStartLimit rebooted;
  assert(rebooted.remaining_ms(0) == 0);

  CompressorStartLimit wrapping;
  const uint32_t first = UINT32_MAX - 30 * minute;
  for (uint32_t n = 0; n < 6; ++n) wrapping.record_transition(0, 1, first + n * 9 * minute);
  assert(wrapping.remaining_ms(first + seventh) == 6 * minute);
  assert(wrapping.remaining_ms(first + hour - 1) == 1);
  assert(wrapping.remaining_ms(first + hour) == 0);
  wrapping.record_transition(0, 1, first + hour);
  assert(wrapping.remaining_ms(first + hour) == 9 * minute);

  // Recurring demand across many ring-buffer turns: no rolling hour admits a
  // seventh command. Run a second pass across the clock wrap as well.
  for (uint32_t origin : {0U, UINT32_MAX - hour}) {
    CompressorStartLimit limit;
    uint32_t admitted[100]{};
    unsigned count = 0;
    for (uint32_t elapsed = 0; elapsed < 12 * hour; elapsed += 9 * minute) {
      const uint32_t now = origin + elapsed;
      if (limit.remaining_ms(now) != 0) continue;
      limit.record_transition(0, 1, now);
      admitted[count++] = now;
      unsigned in_window = 0;
      for (unsigned n = 0; n < count; ++n) {
        if (static_cast<uint32_t>(now - admitted[n]) < hour) ++in_window;
      }
      assert(in_window <= 6);
    }
  }
  return 0;
}
