// Host-test voor de "snelle eerste start" uit upstream OpenQuatt v0.50.0
// (oq_heat_intent_logic.h). Upstream heeft hier geen host-test voor; deze
// legt vast wanneer Power House meteen op de laagste haalbare stand mag starten.

#undef NDEBUG
#include <assert.h>
#include <cmath>
#include <cstdint>

#include "../../openquatt/includes/v050/control/oq_heat_intent_logic.h"

using oq_heat_intent::Decision;
using oq_heat_intent::Input;
using oq_heat_intent::State;

namespace {

Input base(uint32_t now_ms, float room_c, float setpoint_c, bool compressor_active) {
  Input in;
  in.now_ms = now_ms;
  in.strategy_active = true;
  in.heating_enable_valid = true;
  in.heating_enabled = true;
  in.room_fresh = true;
  in.setpoint_fresh = true;
  in.compressor_active = compressor_active;
  in.setpoint_source = 3;  // CIC
  in.room_c = room_c;
  in.setpoint_c = setpoint_c;
  in.room_resume_delta_c = 0.1f;  // Power House comfort below setpoint (default)
  in.setpoint_raise_delta_c = 0.20f;
  in.room_confirm_ms = 10000;
  return in;
}

}  // namespace

int main() {
  // --- Kamervraag: pas na 10 s bevestiging, en alleen zonder draaiende compressor ---
  State s;
  Decision d = oq_heat_intent::evaluate(base(1000, 20.0f, 20.5f, false), s);
  assert(!d.fast_start);  // eerste keer: initialiseren, nog niet bevestigd
  s = d.next;
  d = oq_heat_intent::evaluate(base(6000, 20.0f, 20.5f, false), s);
  assert(!d.fast_start && d.room_condition);
  s = d.next;
  d = oq_heat_intent::evaluate(base(11000, 20.0f, 20.5f, false), s);
  assert(d.fast_start && d.reason == oq_heat_intent::ROOM_DEMAND);
  s = d.next;

  // Compressor start: geen snelle start meer, wel herstel tot halverwege de band.
  d = oq_heat_intent::evaluate(base(16000, 20.0f, 20.5f, true), s);
  assert(!d.fast_start && d.room_recovery_active && d.reason == oq_heat_intent::ROOM_RECOVERY);
  s = d.next;
  // Grens is setpoint - 0.5 * 0.1 = 20.45; net erboven (niet op de floatgrens): herstel vrijgegeven.
  d = oq_heat_intent::evaluate(base(21000, 20.46f, 20.5f, true), s);
  assert(!d.room_recovery_active);
  s = d.next;

  // Kamer binnen de band (20.45 > 20.4): geen kamervraag.
  d = oq_heat_intent::evaluate(base(26000, 20.45f, 20.5f, false), s);
  assert(!d.room_condition && !d.fast_start);
  s = d.next;

  // --- Setpoint verhoogd met >= 0.2 K boven de kamer: meteen snelle start ---
  d = oq_heat_intent::evaluate(base(31000, 20.45f, 20.8f, false), s);
  assert(d.setpoint_raise_edge && d.fast_start && d.reason == oq_heat_intent::SETPOINT_RAISE);
  s = d.next;
  // Setpoint weer omlaag: geannuleerd.
  d = oq_heat_intent::evaluate(base(36000, 20.45f, 20.5f, false), s);
  assert(d.setpoint_raise_cancelled && !d.fast_start);
  s = d.next;
  // Kleine verhoging (0.1 K): geen snelle start.
  d = oq_heat_intent::evaluate(base(41000, 20.45f, 20.6f, false), s);
  assert(!d.setpoint_raise_edge);

  // --- Ongeldige of verouderde invoer: nooit een snelle start ---
  State fresh;
  Input stale = base(1000, 19.0f, 21.0f, false);
  stale.room_fresh = false;
  assert(!oq_heat_intent::evaluate(stale, fresh).fast_start);
  Input no_source = base(1000, 19.0f, 21.0f, false);
  no_source.setpoint_source = 0;
  assert(!oq_heat_intent::evaluate(no_source, fresh).fast_start);
  Input nan_room = base(1000, NAN, 21.0f, false);
  assert(!oq_heat_intent::evaluate(nan_room, fresh).fast_start);
  return 0;
}
