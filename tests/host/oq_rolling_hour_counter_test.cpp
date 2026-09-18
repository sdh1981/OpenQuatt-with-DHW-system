// Host-test voor de emmerteller over een voortschrijdend uur.
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_rolling_hour_counter_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/oq_rolling_hour_counter.h"

int main() {
  using oq_rate::RollingHourCounter;
  constexpr uint32_t minute = 60000U;
  constexpr uint32_t bucket = RollingHourCounter::BUCKET_MS;
  static_assert(sizeof(RollingHourCounter) <= 24U, "Teller moet klein blijven");

  // Leeg is nul, ook zonder dat er ooit iets gebeurd is.
  RollingHourCounter c;
  assert(c.count(0U) == 0U);

  // Drie gebeurtenissen binnen dezelfde emmer.
  c.record(1000U);
  c.record(2000U);
  c.record(3000U);
  assert(c.count(4000U) == 3U);

  // Na een kwartier tellen ze nog mee: het venster is een uur.
  assert(c.count(15U * minute) == 3U);

  // Eén per emmer erbij; na vijf emmers staat de teller op 3 + 5.
  for (uint32_t n = 1; n <= 5; ++n) c.record(n * bucket + 1000U);
  assert(c.count(5U * bucket + 2000U) == 8U);

  // Zes emmers verder is de eerste emmer eruit gelopen: de drie van het begin
  // vervallen, de vijf latere blijven.
  assert(c.count(6U * bucket + 2000U) == 5U);

  // Langer stil dan het hele venster wist alles, ook zonder tussentijdse tik.
  assert(c.count(20U * bucket) == 0U);
  c.record(20U * bucket);
  assert(c.count(20U * bucket + 1000U) == 1U);

  // Een millis()-omslag mag geen sprong van 49 dagen opleveren: vlak voor de
  // omslag tellen, vlak erna nog steeds binnen hetzelfde uur.
  RollingHourCounter wrap;
  const uint32_t before = UINT32_MAX - minute;  // 1 minuut voor de omslag
  wrap.record(before);
  wrap.record(before + 30U * 1000U);
  const uint32_t after = static_cast<uint32_t>(before + 5U * minute);  // 4 min na de omslag
  assert(wrap.count(after) == 2U);
  // En een uur na de omslag zijn ze wel verlopen.
  assert(wrap.count(static_cast<uint32_t>(before + 65U * minute)) == 0U);

  // reset() maakt hem leeg en laat hem opnieuw beginnen.
  c.record(21U * bucket);
  c.reset();
  assert(c.count(21U * bucket + 1000U) == 0U);

  // Verzadiging: de teller mag niet omlopen.
  RollingHourCounter many;
  for (uint32_t n = 0; n < 5000U; ++n) many.record(1000U);
  assert(many.count(2000U) == 5000U);

  return 0;
}
