// Host-test voor de beslissingen achter "tabel toepassen".
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_odu_runtime_frequency_table_logic_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <cmath>
#include <stdint.h>

#include "../../openquatt/includes/oq_odu_runtime_frequency_table_logic.h"

using namespace oq_odu_runtime_frequency;

// Fabriekswaarden uit de EEPROM-dumps van HP1 en HP2 (byte-identiek), gelijk aan
// de defaults in oq_odu_runtime_frequency_table.yaml. F0 is stand 0: uit.
static constexpr std::array<uint16_t, 22> FACTORY = {0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74,
                                                     0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90};

int main() {
  // --- Adressering: doc rekent vanaf 1, de bus vanaf 0 ---
  assert(RUNTIME_TABLE_START_ADDRESS == 3000);
  assert(RUNTIME_TABLE_REGISTER_COUNT == 22);
  assert(GUARD_START_ADDRESS + GUARD_COMPRESSOR_FREQUENCY_INDEX == 2103);

  // --- Parsen van wat de hub aanlevert ---
  FrequencyCurve cooling{};
  FrequencyCurve heating{};
  int loaded = -1;
  assert(parse_runtime_table(std::span<const uint16_t>(FACTORY), cooling, heating, loaded));
  assert(loaded == 22);
  assert(cooling[0] == 0.0f && cooling[1] == 30.0f && cooling[10] == 74.0f);
  assert(heating[0] == 0.0f && heating[1] == 30.0f && heating[10] == 90.0f);
  assert(validate_monotonic_table(cooling) && validate_monotonic_table(heating));

  // Te kort antwoord: faalt, en telt hoe ver het kwam.
  assert(!parse_runtime_table(std::span<const uint16_t>(FACTORY.data(), 7), cooling, heating, loaded));
  assert(loaded == 7);
  // Waarde boven 120 Hz halverwege de verwarmingskromme.
  auto bad = FACTORY;
  bad[15] = 121;
  assert(!parse_runtime_table(std::span<const uint16_t>(bad), cooling, heating, loaded));
  assert(loaded == 15);

  // --- Heen en terug: schrijfwoorden zijn exact de gelezen woorden ---
  assert(parse_runtime_table(std::span<const uint16_t>(FACTORY), cooling, heating, loaded));
  const RuntimeTableWords words = build_runtime_write_values(cooling, heating);
  for (size_t i = 0; i < words.size(); i++) assert(words[i] == FACTORY[i]);
  FrequencyCurve rounded = cooling;
  rounded[3] = 47.4f;  // afronding: 47.4 schrijft 47 en matcht de teruglezing
  assert(build_runtime_write_values(rounded, heating)[3] == 47);
  assert(tables_match(rounded, cooling));
  rounded[3] = 47.6f;
  assert(!tables_match(rounded, cooling));

  // --- Geldigheid en oplopend ---
  assert(!valid_frequency(NAN));
  assert(!valid_frequency(-0.5f));
  assert(valid_frequency(0.0f) && valid_frequency(120.0f));
  assert(!valid_frequency(120.5f));
  FrequencyCurve falling = cooling;
  falling[5] = falling[4] - 1.0f;
  assert(!validate_monotonic_table(falling));
  FrequencyCurve flat = cooling;
  flat[10] = flat[9];  // gelijk is toegestaan, alleen dalen niet
  assert(validate_monotonic_table(flat));

  // --- 5 Hz per stand, symmetrisch, gemeten tegen de unit ---
  assert(check_step_limit(cooling, cooling).ok());
  FrequencyCurve wanted = cooling;
  wanted[2] += 5.0f;
  assert(check_step_limit(wanted, cooling).ok());
  wanted[2] += 1.0f;
  StepCheck step = check_step_limit(wanted, cooling);
  assert(!step.ok() && step.index == 2 && step.delta == 6);
  wanted = cooling;
  wanted[9] -= 6.0f;
  step = check_step_limit(wanted, cooling);
  assert(!step.ok() && step.index == 9 && step.delta == -6);
  // Een oude waarde uit een eerdere proef (bv. F1 op 45 terwijl de unit na een
  // power cycle weer op 30 staat) wordt tegengehouden.
  wanted = cooling;
  wanted[1] = 45.0f;
  step = check_step_limit(wanted, cooling);
  assert(!step.ok() && step.index == 1 && step.delta == 15);
  // Alleen de eerste overtreding wordt gemeld.
  wanted[4] = 80.0f;
  assert(check_step_limit(wanted, cooling).index == 1);

  // --- Bewaking: alleen in stilstand, tenzij expliciet vrijgegeven ---
  assert(decide_guard(0, 0, false) == GuardDecision::ALLOW_STANDSTILL);
  assert(decide_guard(0, 0, true) == GuardDecision::ALLOW_STANDSTILL);
  assert(decide_guard(2, 0, false) == GuardDecision::BLOCK_NOT_STANDBY);
  assert(decide_guard(2, 55, false) == GuardDecision::BLOCK_NOT_STANDBY);
  // Uitlopende compressor in standby telt ook als draaien.
  assert(decide_guard(0, 32, false) == GuardDecision::BLOCK_COMPRESSOR_RUNNING);
  assert(decide_guard(2, 55, true) == GuardDecision::ALLOW_WHILE_RUNNING);
  assert(decide_guard(0, 32, true) == GuardDecision::ALLOW_WHILE_RUNNING);
  return 0;
}
