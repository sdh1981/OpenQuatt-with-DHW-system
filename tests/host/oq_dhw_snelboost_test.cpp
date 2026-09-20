// Host-test voor de handmatige snelboost in de DHW-toestandsmachine.
//
// De aanleiding: de snelboost werd alleen in IDLE_CV gelezen, dus een druk op de
// knop tijdens een lopende cyclus deed niets -- en werd aan het eind van die
// cyclus ook nog weggegooid. Deze test legt vast dat een snelboost vanuit elke
// lopende warmwatertoestand aankomt, mét de bijbehorende grenzen.
//
//   g++ -std=c++20 -Wall -Wextra -Werror tests/host/oq_dhw_snelboost_test.cpp -o /tmp/t && /tmp/t

#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/oq_dhw_controller_logic.h"

namespace {

using oq_dhw::Config;
using oq_dhw::Controller;
using oq_dhw::Inputs;
using oq_dhw::State;

Config make_config() {
  Config cfg;
  cfg.boost_hp_assist_enable = false;  // standaard uit; de snelboost hoort dat te negeren
  return cfg;
}

// Klep meldt de DHW-stand, sensoren plausibel, geen storingen.
Inputs make_inputs(uint32_t now_ms, float top_c, float bottom_c) {
  Inputs in;
  in.now_ms = now_ms;
  in.tank_top_c = top_c;
  in.tank_bottom_c = bottom_c;
  in.valve_feedback_valid = true;
  in.valve_feedback_cv = false;  // false = klep staat op DHW
  in.flow_valid = false;
  return in;
}

// Draait de machine van IDLE_CV naar DHW_HEAT_PUMP: klep bevestigt, en top_c
// moet onder de startdrempel (46 C) liggen, anders begint er niets.
uint32_t run_to_heat_pump(Controller &c, const Config &cfg, uint32_t now_ms, float top_c, float bottom_c) {
  assert(top_c < cfg.start_top_c && "tank te warm om een cyclus te starten");
  for (int i = 0; i < 20; ++i) {
    const auto out = c.tick(make_inputs(now_ms, top_c, bottom_c), cfg);
    if (out.state == State::DHW_HEAT_PUMP) return now_ms;
    now_ms += 2000U;
  }
  assert(false && "DHW_HEAT_PUMP niet bereikt");
  return now_ms;
}

// Een gewone cyclus die in DHW_HEAT_PUMP staat, waarna de knop aangaat: dat hoort
// meteen een snelboost te worden. Element aan, HP's blijven meedraaien, en het
// doel is het snelboost-doel en niet het gewone boost-doel.
void test_upgrade_from_heat_pump() {
  const Config cfg = make_config();
  Controller c;
  uint32_t now = 1000U;
  now = run_to_heat_pump(c, cfg, now, 40.0f, 30.0f);
  assert(!c.max_boost_active());

  now += 2000U;
  Inputs in = make_inputs(now, 40.0f, 30.0f);
  in.max_boost_request = true;
  const auto out = c.tick(in, cfg);

  assert(out.state == State::DHW_BOOST);
  assert(c.max_boost_active());
  assert(out.element_on);
  assert(out.hp_dhw_request);  // HP-assist draait mee, ook met boost_hp_assist_enable uit
  assert(out.valve_to_boiler);
}

// Boven het snelboost-doel valt er niets te verwarmen: dan geen overgang, en de
// cyclus loopt gewoon door.
void test_no_upgrade_above_target() {
  const Config cfg = make_config();
  Controller c;
  uint32_t now = 1000U;
  now = run_to_heat_pump(c, cfg, now, 40.0f, 30.0f);

  now += 2000U;
  Inputs in = make_inputs(now, cfg.max_boost_target_c + 1.0f, 30.0f);
  in.max_boost_request = true;
  const auto out = c.tick(in, cfg);
  assert(out.state != State::DHW_BOOST || !c.max_boost_active());
  assert(!c.max_boost_active());
}

// Een lockout of HP-storing blokkeert de opwaardering, net als een start.
void test_no_upgrade_when_blocked() {
  const Config cfg = make_config();
  Controller c;
  uint32_t now = 1000U;
  now = run_to_heat_pump(c, cfg, now, 40.0f, 30.0f);

  now += 2000U;
  Inputs in = make_inputs(now, 40.0f, 30.0f);
  in.max_boost_request = true;
  in.lockout_active = true;
  c.tick(in, cfg);
  assert(!c.max_boost_active());
}

// Opwaarderen tijdens het natraject (element maakt af na de HP-fase): het doel
// gaat omhoog en de HP's komen erbij.
void test_upgrade_from_regular_boost() {
  Config cfg = make_config();
  cfg.hp_max_runtime_ms = 1000U;  // duw de HP-fase snel naar het natraject
  Controller c;
  uint32_t now = 1000U;
  now = run_to_heat_pump(c, cfg, now, 44.0f, 35.0f);

  // HP-fase loopt af op de tijd; tank onder boost_target, dus natraject.
  now += 5000U;
  auto out = c.tick(make_inputs(now, 44.0f, 35.0f), cfg);
  assert(out.state == State::DHW_BOOST);
  assert(!c.max_boost_active());
  assert(out.element_on);          // element maakt af tot boost_target_c (56)
  assert(!out.hp_dhw_request);     // geen assist: die stond uit

  // Nu de knop. Doel wordt 60 en de HP's komen erbij, want 44 < 55.
  now += 2000U;
  Inputs in = make_inputs(now, 44.0f, 35.0f);
  in.max_boost_request = true;
  out = c.tick(in, cfg);
  assert(out.state == State::DHW_BOOST);
  assert(c.max_boost_active());
  assert(out.hp_dhw_request);
  assert(out.element_on);

  // Tussen het oude en het nieuwe doel blijft het element aan: dat is het punt.
  now += 2000U;
  in = make_inputs(now, 58.0f, 45.0f);
  in.max_boost_request = true;
  out = c.tick(in, cfg);
  assert(out.state == State::DHW_BOOST);
  assert(out.element_on);
  assert(!out.hp_dhw_request);  // boven 55 gaan de HP's eruit, element gaat door
}

// De thermische grens van de YAML-laag heeft de HP's er net uitgehaald. Een druk
// op de knop mag ze dan niet meteen terughalen -- opnieuw opstarten in een hete
// tank levert alleen drukpieken op. Het element mag wel door naar het hogere doel.
void test_upgrade_respects_thermal_limit() {
  Config cfg = make_config();
  cfg.hp_max_runtime_ms = 1000U;
  Controller c;
  uint32_t now = 1000U;
  now = run_to_heat_pump(c, cfg, now, 44.0f, 35.0f);
  now += 5000U;
  auto out = c.tick(make_inputs(now, 44.0f, 35.0f), cfg);
  assert(out.state == State::DHW_BOOST);

  now += 2000U;
  Inputs in = make_inputs(now, 44.0f, 35.0f);
  in.max_boost_request = true;
  in.hp_thermal_limit_active = true;
  out = c.tick(in, cfg);
  assert(c.max_boost_active());
  assert(!out.hp_dhw_request);
  assert(out.element_on);
}

// De knop weer uitzetten breekt ook een opgewaardeerde snelboost af.
void test_release_aborts_upgraded_boost() {
  const Config cfg = make_config();
  Controller c;
  uint32_t now = 1000U;
  now = run_to_heat_pump(c, cfg, now, 40.0f, 30.0f);

  now += 2000U;
  Inputs in = make_inputs(now, 40.0f, 30.0f);
  in.max_boost_request = true;
  auto out = c.tick(in, cfg);
  assert(out.state == State::DHW_BOOST && c.max_boost_active());

  now += 2000U;
  out = c.tick(make_inputs(now, 40.0f, 30.0f), cfg);
  assert(out.state == State::IDLE_CV);
  assert(!c.max_boost_active());
  assert(!out.element_on);
}

}  // namespace

int main() {
  test_upgrade_from_heat_pump();
  test_no_upgrade_above_target();
  test_no_upgrade_when_blocked();
  test_upgrade_from_regular_boost();
  test_upgrade_respects_thermal_limit();
  test_release_aborts_upgraded_boost();
  return 0;
}
