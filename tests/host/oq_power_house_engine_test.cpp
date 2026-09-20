// Host-test voor de eigenaarskeuze tussen de fork- en de v0.50-motor.
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_power_house_engine_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/oq_power_house_engine.h"

namespace {

using namespace oq_ph_engine;

void reset_state() { state() = State{}; }

OwnerInput good(uint32_t now_ms, uint8_t selected, bool running = false) {
  OwnerInput in;
  in.now_ms = now_ms;
  in.selected = selected;
  in.strategy_active = true;
  in.output_valid = true;
  in.tables_known = true;
  in.handover_blocked = false;
  in.compressors_running = running;
  return in;
}

// Bij het opstarten mag er geen settle-venster staan: dan zou de eerste start
// drie minuten worden tegengehouden.
void test_boot_has_no_settle_window() {
  reset_state();
  decide_owner(good(1000U, V050));
  assert(state().owner == V050);
  assert(state().fallback == OK);
  assert(!settling(1000U));
  const auto out = settle_clamp(1000U, 0, 0, 4, 0);
  assert(out.hp1 == 4 && out.hp2 == 0);
  assert(v050_drives(1000U));
}

// Zonder frequentietabel rekent het Hz-model op niets: dan stuurt de fork, met
// de reden erbij.
void test_falls_back_without_table() {
  reset_state();
  auto in = good(1000U, V050);
  in.tables_known = false;
  decide_owner(in);
  assert(state().owner == FORK);
  assert(state().fallback == TABLE_UNKNOWN);
  assert(fork_drives(1000U));

  // Tabel komt binnen: v0.50 neemt het over.
  decide_owner(good(2000U, V050));
  assert(state().owner == V050);
  assert(state().fallback == OK);
  assert(v050_drives(2000U));
}

// Een onbruikbaar prestatiemodel geeft de besturing NIET terug: upstream houdt
// dan de draaiende standen vast. Eromheen wisselen zou bij elke stop een wissel
// opleveren, want twee stilstaande units in hun minimale uit-tijd tellen al als
// "model niet bruikbaar".
void test_model_hold_keeps_the_owner() {
  reset_state();
  decide_owner(good(1000U, V050));
  auto in = good(2000U, V050, true);
  in.output_valid = false;
  decide_owner(in);
  assert(state().owner == V050);
  assert(state().fallback == MODEL_HOLD);
  assert(!settling(2000U));  // geen wissel, dus geen venster
}

// Wisselen terwijl alles stilstaat hoeft niet begrensd te worden: er is niets
// te verstoren, en een venster zou de eerstvolgende start drie minuten kosten.
void test_switch_without_running_compressors_has_no_window() {
  reset_state();
  decide_owner(good(1000U, FORK));
  decide_owner(good(2000U, V050, false));
  assert(state().owner == V050);
  assert(!settling(2000U));
  const auto out = settle_clamp(3000U, 0, 0, 5, 0);
  assert(out.hp1 == 5 && out.hp2 == 0);
}

// Een wissel tijdens defrost of oliehold wordt uitgesteld tot die voorbij is.
void test_handover_is_deferred_while_blocked() {
  reset_state();
  decide_owner(good(1000U, FORK, true));
  auto in = good(2000U, V050, true);
  in.handover_blocked = true;
  decide_owner(in);
  assert(state().owner == FORK);
  assert(state().fallback == HANDOVER_PENDING);

  decide_owner(good(3000U, V050, true));
  assert(state().owner == V050);
  assert(state().fallback == OK);
}

// Na een echte wissel: drie minuten waarin niets start, niets stopt en de stand
// hooguit één stap beweegt.
void test_settle_window_blocks_start_and_stop() {
  reset_state();
  decide_owner(good(1000U, FORK, true));
  decide_owner(good(2000U, V050, true));  // wissel terwijl er iets draait
  assert(settling(2000U));

  // HP1 draait op 4 en zou naar 8 willen: hooguit 5.
  // HP2 staat stil en zou willen starten: blijft uit.
  auto out = settle_clamp(3000U, 4, 0, 8, 6);
  assert(out.hp1 == 5 && out.hp2 == 0);

  // Zou HP1 willen stoppen, dan blijft hij op 3 draaien.
  out = settle_clamp(4000U, 4, 0, 0, 0);
  assert(out.hp1 == 3 && out.hp2 == 0);

  // Precies op de grens loopt het venster af.
  assert(settling(2000U + kSettleMs - 1U));
  assert(!settling(2000U + kSettleMs));
  out = settle_clamp(2000U + kSettleMs, 4, 0, 8, 6);
  assert(out.hp1 == 8 && out.hp2 == 6);
}

// Zonder wissel is er niets te begrenzen, ook niet vlak na het opstarten.
void test_no_clamp_without_switch() {
  reset_state();
  decide_owner(good(1000U, V050));
  decide_owner(good(11000U, V050));
  const auto out = settle_clamp(11000U, 0, 3, 7, 0);
  assert(out.hp1 == 7 && out.hp2 == 0);
}

// Zwijgt de v0.50-lus, dan neemt de fork over -- ook al blijft de keuze staan.
void test_watchdog_hands_back_to_fork() {
  reset_state();
  decide_owner(good(1000U, V050));
  assert(v050_drives(1000U));
  assert(v050_drives(1000U + kStaleMs));
  assert(fork_drives(1000U + kStaleMs + 1U));
  // De keuze zelf verandert niet: zodra de lus weer meldt, stuurt hij weer.
  assert(state().owner == V050);
  decide_owner(good(200000U, V050));
  assert(v050_drives(200000U));
}

// Koelen of warm water: Power House ligt stil, de reden zegt dat ook.
void test_inactive_strategy_is_reported() {
  reset_state();
  auto in = good(1000U, V050);
  in.strategy_active = false;
  decide_owner(in);
  assert(state().fallback == STRATEGY_INACTIVE);
}

// millis() loopt om: de waakhond mag daar geen uur van maken.
void test_watchdog_survives_millis_wrap() {
  reset_state();
  const uint32_t before = UINT32_MAX - 10000U;
  decide_owner(good(before, V050));
  const uint32_t after = static_cast<uint32_t>(before + 20000U);  // 10 s na de omslag
  assert(v050_drives(after));
  assert(fork_drives(static_cast<uint32_t>(before + kStaleMs + 1U)));
}

}  // namespace

int main() {
  test_boot_has_no_settle_window();
  test_falls_back_without_table();
  test_model_hold_keeps_the_owner();
  test_switch_without_running_compressors_has_no_window();
  test_handover_is_deferred_while_blocked();
  test_settle_window_blocks_start_and_stop();
  test_no_clamp_without_switch();
  test_watchdog_hands_back_to_fork();
  test_inactive_strategy_is_reported();
  test_watchdog_survives_millis_wrap();
  return 0;
}
