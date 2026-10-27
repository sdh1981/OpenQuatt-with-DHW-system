// Host-test voor de halve compressorstanden (fase 1: alleen rekenen, niet schrijven).
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_odu_halfstep_levels_logic_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <cmath>
#include <stdint.h>

#include "../../openquatt/includes/oq_odu_halfstep_levels_logic.h"

using namespace oq_odu_halfstep;

// Verwarmingskromme uit de EEPROM van HP1 en HP2 (byte-identiek).
static constexpr FrequencyCurve FACTORY_HEATING = {0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90};

static Inputs base_inputs() {
  Inputs in{};
  in.now_ms = 3600UL * 1000UL;
  in.working_mode = 2;
  in.defrosting = false;
  in.oil_return = false;
  in.frequency_limited = false;
  in.last_start_ms = 1000UL;            // lang geleden gestart
  in.last_defrost_end_ms = 1000UL;
  in.requested_half_level = 10;         // stand 5, kromme B
  in.request_since_ms = in.now_ms - REQUEST_STABLE_MS;
  in.active_curve = Curve::A;
  in.last_switch_ms = in.now_ms - MIN_SWITCH_INTERVAL_MS;
  in.curve_b_usable = true;
  return in;
}

int main() {
  // --- Halve stand naar stand plus kromme ---
  assert(level_of_half(0) == 0 && curve_of_half(0) == Curve::A);
  assert(level_of_half(1) == 1 && curve_of_half(1) == Curve::A);
  assert(level_of_half(2) == 1 && curve_of_half(2) == Curve::B);
  assert(level_of_half(19) == 10 && curve_of_half(19) == Curve::A);
  assert(clamp_half_level(-3) == 0 && clamp_half_level(99) == MAX_HALF_LEVEL);

  // --- Kromme B uit de fabriekskromme ---
  FrequencyCurve b{};
  assert(build_curve_b(FACTORY_HEATING, b));
  const float expected_b[CURVE_POINTS] = {0, 34, 44, 52, 58, 64, 69, 75, 82, 87, 90};
  for (size_t i = 0; i < CURVE_POINTS; i++) assert(b[i] == expected_b[i]);
  // F0 en F10 blijven ongemoeid.
  assert(b[0] == FACTORY_HEATING[0] && b[10] == FACTORY_HEATING[10]);

  // Elke verschuiving blijft binnen de bewaking van de bestaande schrijfketen.
  for (size_t i = 1; i + 1 < CURVE_POINTS; i++) {
    const float shift = b[i] - FACTORY_HEATING[i];
    assert(shift >= 1.0f && shift <= (float) MAX_SHIFT_HZ);
    assert(b[i] < FACTORY_HEATING[i + 1]);  // blijft onder de volgende stand
  }

  // --- De ladder loopt op en heeft geen gat groter dan 5 Hz ---
  float prev = 0.0f;
  for (int half = 1; half <= MAX_HALF_LEVEL; half++) {
    const float hz = frequency_of_half(FACTORY_HEATING, b, half);
    assert(hz > prev);
    if (half > 1) assert(hz - prev <= 5.0f);
    prev = hz;
  }
  assert(frequency_of_half(FACTORY_HEATING, b, 1) == 30.0f);
  assert(frequency_of_half(FACTORY_HEATING, b, 19) == 90.0f);
  assert(frequency_of_half(FACTORY_HEATING, b, 0) == 0.0f);

  // --- Krommes die niet deugen worden geweigerd ---
  FrequencyCurve bad = FACTORY_HEATING;
  bad[3] = bad[2];  // niet oplopend
  assert(!build_curve_b(bad, b));
  bad = FACTORY_HEATING;
  bad[5] = 95.0f;   // boven de bovengrens verderop in de kromme
  bad[6] = 96.0f;
  bad[7] = 97.0f;
  bad[8] = 98.0f;
  bad[9] = 99.0f;
  bad[10] = 100.0f;
  assert(!build_curve_b(bad, b));
  bad = FACTORY_HEATING;
  bad[1] = 20.0f;   // onder de ondergrens
  assert(!build_curve_b(bad, b));
  bad = FACTORY_HEATING;
  bad[9] = 89.0f;   // gat van 1 Hz naar F10: geen halve stap te maken
  assert(!build_curve_b(bad, b));

  // --- Geen wisseling nodig: zelfde kromme ---
  {
    Inputs in = base_inputs();
    in.requested_half_level = 9;  // stand 5, kromme A
    const Decision d = evaluate(in);
    assert(d.level == 5 && d.curve == Curve::A);
    assert(!d.switch_needed && !d.may_switch && d.block == Block::NONE);
  }

  // --- Alles in orde: wisselen mag ---
  {
    const Decision d = evaluate(base_inputs());
    assert(d.level == 5 && d.curve == Curve::B);
    assert(d.switch_needed && d.may_switch && d.block == Block::NONE);
  }

  // --- Elke blokkade afzonderlijk ---
  {
    Inputs in = base_inputs(); in.working_mode = 1;
    assert(evaluate(in).block == Block::MODE_NOT_HEATING);
  }
  {
    Inputs in = base_inputs(); in.defrosting = true;
    assert(evaluate(in).block == Block::DEFROSTING);
  }
  {
    Inputs in = base_inputs(); in.oil_return = true;
    assert(evaluate(in).block == Block::OIL_RETURN);
  }
  {
    Inputs in = base_inputs(); in.last_start_ms = in.now_ms - (AFTER_START_BLOCK_MS - 1000UL);
    assert(evaluate(in).block == Block::AFTER_START);
  }
  {
    Inputs in = base_inputs(); in.last_defrost_end_ms = in.now_ms - (AFTER_DEFROST_BLOCK_MS - 1000UL);
    assert(evaluate(in).block == Block::AFTER_DEFROST);
  }
  {
    Inputs in = base_inputs(); in.frequency_limited = true;
    assert(evaluate(in).block == Block::FREQUENCY_LIMITED);
  }
  {
    Inputs in = base_inputs(); in.last_switch_ms = in.now_ms - (MIN_SWITCH_INTERVAL_MS - 1000UL);
    assert(evaluate(in).block == Block::TOO_SOON);
  }
  {
    Inputs in = base_inputs(); in.request_since_ms = in.now_ms - (REQUEST_STABLE_MS - 1000UL);
    assert(evaluate(in).block == Block::NOT_STABLE);
  }
  {
    Inputs in = base_inputs(); in.curve_b_usable = false;
    assert(evaluate(in).block == Block::CURVE_UNUSABLE);
  }

  // --- Randgevallen rond de tijdvensters ---
  {
    // Precies op de grens telt als verstreken.
    Inputs in = base_inputs(); in.last_start_ms = in.now_ms - AFTER_START_BLOCK_MS;
    assert(evaluate(in).may_switch);
  }
  {
    // Nog nooit gewisseld of ontdooid: geen blokkade.
    Inputs in = base_inputs(); in.last_switch_ms = 0; in.last_defrost_end_ms = 0;
    assert(evaluate(in).may_switch);
  }
  {
    // Overloop van millis() mag geen blokkade opleveren.
    Inputs in = base_inputs();
    in.now_ms = 1000UL;
    in.last_start_ms = 0xFFFFFFFFUL - AFTER_START_BLOCK_MS;
    in.last_defrost_end_ms = 0xFFFFFFFFUL - AFTER_DEFROST_BLOCK_MS;
    in.last_switch_ms = 0xFFFFFFFFUL - MIN_SWITCH_INTERVAL_MS;
    in.request_since_ms = 0xFFFFFFFFUL - REQUEST_STABLE_MS;
    assert(evaluate(in).may_switch);
  }

  // --- Stand 0: uit, en nooit een wisseling waard ---
  {
    Inputs in = base_inputs(); in.requested_half_level = 0;
    const Decision d = evaluate(in);
    assert(d.level == 0 && !d.switch_needed && !d.may_switch);
  }

  // --- Teksten zijn gevuld ---
  assert(block_text(Block::NONE)[0] != '\0');
  assert(block_text(Block::TOO_SOON)[0] != '\0');
  assert(curve_text(Curve::A)[0] == 'A' && curve_text(Curve::B)[0] == 'B');

  return 0;
}
