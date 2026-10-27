// Host-test voor de sonde op de slots 3051-3070.
//
// Wat hier vastligt is vooral wat de sonde NIET mag: buiten het bereik
// schrijven, het vastlegcommando naar EEPROM aanraken, of een waarde van een
// andere orde van grootte doorlaten in registers waarvan we de betekenis niet
// kennen.
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_odu_slot_probe_logic_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include <string>

#include "../../openquatt/includes/oq_odu_slot_probe_logic.h"

namespace {

using namespace oq_odu_slot_probe;

bool contains(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

void test_address_range() {
  assert(valid_sheet_address(3051));
  assert(valid_sheet_address(3070));
  assert(!valid_sheet_address(3050));  // dat is de ladder, niet de sonde
  assert(!valid_sheet_address(3071));
  assert(!valid_sheet_address(0));

  // Modbus-adres is bladadres min 1, dezelfde regel als bij de ladder.
  assert(modbus_address(3051) == 3050);
  assert(modbus_address(3070) == 3069);
  // Een afgekeurd adres levert 0 op, en 0 wordt nooit geschreven.
  assert(modbus_address(3071) == 0);
}

// Het vastlegcommando naar EEPROM mag nooit door deze sonde heen komen, ook niet
// als iemand het adresbereik later oprekt.
void test_persist_register_is_never_valid() {
  assert(!valid_sheet_address(FORBIDDEN_PERSIST_SHEET_ADDRESS));
  assert(modbus_address(FORBIDDEN_PERSIST_SHEET_ADDRESS) == 0);
  static_assert(FORBIDDEN_PERSIST_SHEET_ADDRESS > LAST_SHEET_ADDRESS,
                "5000 moet buiten het sondebereik blijven liggen");
}

void test_value_range() {
  assert(valid_value(0));
  assert(valid_value(100));
  assert(!valid_value(101));
  assert(!valid_value(60000));
  static_assert(MAX_VALUE == 100, "onbekende parameters: klein houden");
}

void test_restore_timing() {
  ProbeState state;
  assert(!restore_due(state, 1000));  // niets geschreven, niets te herstellen

  state.restore_pending = true;
  state.written_ms = 1000;
  assert(!restore_due(state, 1000));
  assert(!restore_due(state, 1000 + RESTORE_AFTER_MS - 1));
  assert(restore_due(state, 1000 + RESTORE_AFTER_MS));

  // millis() loopt om: dat mag geen twee minuten extra opleveren.
  ProbeState wrap;
  wrap.restore_pending = true;
  wrap.written_ms = UINT32_MAX - 1000;
  assert(!restore_due(wrap, static_cast<uint32_t>(wrap.written_ms + 1000)));
  assert(restore_due(wrap, static_cast<uint32_t>(wrap.written_ms + RESTORE_AFTER_MS)));
}

void test_result_and_verdict() {
  const std::string text = result_text(3055, 0, 1, 1);
  assert(text == "3055: was 0, geschreven 1, teruggelezen 1");

  // De uitkomst van de proef, in woorden.
  assert(contains(verdict_text(1, 1), "vastgehouden"));
  assert(contains(verdict_text(1, 0), "genegeerd"));
  assert(contains(verdict_text(1, 7), "afwijkend"));
  // Nul over nul schrijven is de eerste, ongevaarlijke proef: dat telt als
  // vastgehouden, niet als genegeerd.
  assert(contains(verdict_text(0, 0), "vastgehouden"));

  assert(restored_text(3055, 0, 0) == "3055: hersteld naar 0, teruggelezen 0");
  assert(contains(refused_text("de compressor draait"), "GEWEIGERD"));
}

}  // namespace

int main() {
  test_address_range();
  test_persist_register_is_never_valid();
  test_value_range();
  test_restore_timing();
  test_result_and_verdict();
  return 0;
}
