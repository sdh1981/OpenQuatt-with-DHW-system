// Host-test voor de uitleg van compressorstanden, plafond en slots.
//
// De belangrijkste eis staat onderaan: de sloturekst mag NOOIT "Hz" bevatten.
// Op R32 zijn die registers geen frequentieladder, en zodra iemand ze zo ziet
// staan gaat er iemand omrekenen.
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_odu_step_ceiling_logic_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include <array>
#include <string>

#include "../../openquatt/includes/oq_odu_step_ceiling_logic.h"

namespace {

using namespace oq_odu_steps;

bool contains(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

// Modbus-adres is bladadres min 1, dezelfde regel als bij de ladder.
void test_addresses_follow_the_sheet_offset() {
  static_assert(SLOT_START_ADDRESS == 3050, "blad 3051 -> modbus 3050");
  static_assert(SLOT_REGISTER_COUNT == 20, "blad 3051..3070 = twintig registers");
  static_assert(SLOT_GROUP_SIZE == 10, "twee groepen van tien");
}

// Alleen de twee bekende R32-versies tellen. Een onbekende versie hoort eerlijk
// te melden dat dit niet van toepassing is, niet stilzwijgend tien aan te nemen.
void test_version_gate_is_strict() {
  assert(is_r32_firmware(0x0119));  // V1, 1.25
  assert(is_r32_firmware(0x011E));  // V1.5, 1.30
  assert(!is_r32_firmware(0x011F));  // 1.31 bestaat niet; niet zomaar meenemen
  assert(!is_r32_firmware(0x0201));  // ander platform
  assert(!is_r32_firmware(0x0000));

  assert(step_ceiling(0x011E) == 10);
  assert(step_ceiling(0x0201) == 0);

  assert(firmware_text(0x0119) == "1.25");
  assert(firmware_text(0x011E) == "1.30");
}

void test_ladder_text() {
  const std::array<uint16_t, 11> hz{0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90};
  const std::string text = ladder_text(hz, true, 5, 0);
  assert(contains(text, "0,30,39,49,55,61,67,72,79,85,90"));
  assert(contains(text, "Hz"));          // dit ZIJN frequenties
  assert(contains(text, "F0-F10"));
  assert(contains(text, "5 min oud"));

  // Nog niet gelezen: zeg dat, met het aantal pogingen erbij.
  const std::string unknown = ladder_text(hz, false, 0, 3);
  assert(contains(unknown, "onbekend"));
  assert(contains(unknown, "3"));
  assert(!contains(unknown, "Hz"));
}

// Twintig rauwe woorden, twee groepen van tien, decimaal.
void test_slot_text_is_raw_decimal() {
  std::array<uint16_t, 20> words{};
  for (size_t i = 0; i < words.size(); ++i) words[i] = static_cast<uint16_t>(100 + i);

  const std::string first = slot_text(words, 0, true, true, 0);
  assert(first == "100,101,102,103,104,105,106,107,108,109");

  const std::string second = slot_text(words, SLOT_GROUP_SIZE, true, true, 0);
  assert(second == "110,111,112,113,114,115,116,117,118,119");
}

// De kern van deze feature: geen enkele slotweergave mag naar frequentie ruiken.
void test_slot_text_never_mentions_hz() {
  std::array<uint16_t, 20> words{};
  for (size_t i = 0; i < words.size(); ++i) words[i] = static_cast<uint16_t>(i * 7);

  for (size_t first : {static_cast<size_t>(0), SLOT_GROUP_SIZE}) {
    for (bool applicable : {true, false}) {
      for (bool known : {true, false}) {
        const std::string text = slot_text(words, first, applicable, known, 2);
        assert(!contains(text, "Hz"));
        assert(!contains(text, "hz"));
        assert(!contains(text, "frequentie"));
      }
    }
  }
}

void test_slot_text_states() {
  std::array<uint16_t, 20> words{};
  assert(slot_text(words, 0, false, true, 0) == "niet van toepassing");
  assert(contains(slot_text(words, 0, true, false, 2), "onbekend"));
  // Te weinig woorden binnen: niet half tonen.
  std::array<uint16_t, 4> short_words{1, 2, 3, 4};
  assert(contains(slot_text(short_words, 0, true, true, 0), "onbekend"));
}

void test_status_text() {
  assert(contains(status_text(false, 0, false, 0), "wacht op firmwareversie"));

  const std::string other = status_text(true, 0x0201, false, 0);
  assert(contains(other, "niet van toepassing"));
  assert(contains(other, "2.1"));

  const std::string ready = status_text(true, 0x011E, true, 0);
  assert(contains(ready, "R32 fw 1.30"));
  assert(contains(ready, "plafond 10 standen"));
  assert(contains(ready, "niet instelbaar"));

  const std::string waiting = status_text(true, 0x011E, false, 0);
  assert(contains(waiting, "nog niet gelezen"));

  const std::string failed = status_text(true, 0x0119, false, 3);
  assert(contains(failed, "mislukt"));
  assert(contains(failed, "3"));
}

}  // namespace

int main() {
  test_addresses_follow_the_sheet_offset();
  test_version_gate_is_strict();
  test_ladder_text();
  test_slot_text_is_raw_decimal();
  test_slot_text_never_mentions_hz();
  test_slot_text_states();
  test_status_text();
  return 0;
}
