// Host-test voor de aansluiting van upstream Power House v0.50.0 op deze fork.
//
// Het belangrijkste deel: de vraagberekening van upstream PLUS de v0.32-
// uitbreidingen moet exact hetzelfde vermogen, dezelfde vraag f en hetzelfde
// comfortgeheugen opleveren als de huidige fork-YAML. ForkReference hieronder is
// een letterlijke naschrijving van oq_power_house_strategy.yaml (het deel dat
// P_req en oq_demand_raw uitrekent), zonder de toestand van de uitbreidingen zelf
// (zon-EMA, tariefslew, PV-slew, raamdetectie): die leest de schaduw uit.

#undef NDEBUG
#include <assert.h>
#include <cmath>
#include <cstdint>

#include "../../openquatt/includes/oq_power_house_v050_adapter.h"

namespace {

constexpr float kTc = -10.0f, kT0 = 16.0f, kPr = 7020.0f, kKp = 3000.0f;
constexpr float kBelow = 0.1f, kAbove = 0.3f, kRise = 8.0f, kFall = 3.0f;
constexpr int kMaxF = 20;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct ForkReference {
  uint32_t last_ms = 0;
  float last_w = 0.0f;
  float memory_c = 0.0f;
  int demand_raw = 0;

  float step(uint32_t now_ms, float Tout, float Tr, float Trsp, float water, float solar_correction_w,
             float preheat_w, float tariff_shift_c, float pv_boost_w, bool window_open) {
    float x = clampf((kT0 - Tout) / (kT0 - kTc), 0.0f, 1.0f);
    float P_house = kPr * x;
    P_house = std::fmax(0.0f, P_house - solar_correction_w);

    const float Tr_low_base = Trsp - kBelow;
    const float Tr_high_base = Trsp + kAbove;
    const float Tr_mid_base = Tr_low_base + 0.5f * (Tr_high_base - Tr_low_base);
    uint32_t last = last_ms;
    float lastw = last_w;
    if (last == 0) {
      last = now_ms;
      lastw = P_house;
    }
    const float dt_power_s = (now_ms >= last) ? ((now_ms - last) / 1000.0f) : 0.0f;
    const float memory_max = clampf(0.05f + (0.50f * kAbove), 0.08f, 0.20f);
    float memory = memory_c;
    if (Tr < Tr_low_base) {
      const float u = clampf((Tr_low_base - Tr) / 0.45f, 0.0f, 1.0f);
      const float build_min = memory_max / 90.0f;
      const float build_max = memory_max / 24.0f;
      memory += (build_min + (build_max - build_min) * u) / 60.0f * dt_power_s;
    } else if (Tr <= Tr_mid_base) {
    } else if (Tr <= Tr_high_base) {
      memory -= (memory_max / 40.0f) / 60.0f * dt_power_s;
    } else {
      memory -= (memory_max / 12.0f) / 60.0f * dt_power_s;
    }
    memory_c = clampf(memory, 0.0f, memory_max);

    const float Tr_low = Trsp + memory_c - kBelow;
    float e = 0.0f;
    if (Tr < Tr_low)
      e = Tr_low - Tr;
    else if (Tr > Trsp)
      e = Trsp - Tr;
    float P_raw = P_house + kKp * e;
    if (preheat_w > 0.0f && preheat_w > P_raw) P_raw = preheat_w;
    P_raw = clampf(P_raw, 0.0f, kPr);

    const float up = (kPr / kRise) / 60.0f;
    const float dn = (kPr / kFall) / 60.0f;
    float P_limited = P_raw;
    if (dt_power_s > 0.0f) {
      if (P_raw > lastw)
        P_limited = std::fmin(P_raw, lastw + up * dt_power_s);
      else if (P_raw < lastw)
        P_limited = std::fmax(P_raw, lastw - dn * dt_power_s);
    }
    float P_effective = P_limited * water;
    const float P_tariff_w = clampf(kKp * tariff_shift_c, -kPr, kPr);
    P_effective = std::fmax(0.0f, std::fmin(kPr * 1.20f, P_effective + P_tariff_w + pv_boost_w));
    if (window_open) P_effective = 0.0f;

    last_w = P_effective;
    last_ms = now_ms;
    long raw = std::lround(20.0f * (P_effective / kPr));
    demand_raw = static_cast<int>(raw < 0 ? 0 : (raw > kMaxF ? kMaxF : raw));
    return P_effective;
  }
};

void assert_near(float a, float b, float tolerance) {
  assert(std::isfinite(a) && std::isfinite(b));
  assert(std::fabs(a - b) <= tolerance);
}

void test_demand_matches_fork() {
  ForkReference fork;
  oq_power_house::DemandState state;
  const oq_power_house::DemandTuning tuning{0.5f, kKp, kBelow, kAbove, kRise, kFall, kMaxF};

  for (int step = 0; step < 360; ++step) {
    const uint32_t now = 1000U + static_cast<uint32_t>(step) * 10000U;
    // Scenario van een uur: kamer loopt van onder naar boven het setpoint en terug.
    const float t = static_cast<float>(step);
    const float room = 20.5f + 0.6f * std::sin(t / 40.0f) - 0.2f;
    const float outside = 3.0f + 4.0f * std::sin(t / 90.0f);
    const float setpoint = 20.5f;
    const float water = (step >= 60 && step < 90) ? 0.8f : 1.0f;
    // Elke uitbreiding een eigen stuk van het uur, en een stuk met alles tegelijk.
    const float solar = (step >= 30 && step < 150) ? 450.0f : 0.0f;
    const float preheat = (step >= 120 && step < 200) ? 2400.0f : 0.0f;
    const float tariff_shift = (step >= 180 && step < 240) ? 0.3f : ((step >= 240 && step < 270) ? -0.25f : 0.0f);
    const float pv = (step >= 200 && step < 260) ? 900.0f : 0.0f;
    const bool window = (step >= 290 && step < 310);

    const float fork_w = fork.step(now, outside, room, setpoint, water, solar, preheat, tariff_shift, pv, window);

    oq_power_house::DemandInput in;
    in.now_ms = now;
    in.outside_c = outside;
    in.cold_c = kTc;
    in.zero_power_c = kT0;
    in.rated_w = kPr;
    in.room_c = room;
    in.setpoint_c = setpoint;
    in.water_limit_factor = water;
    oq_ph_v050::DemandExtensions ext;
    ext.solar_correction_w = solar;
    ext.preheat_floor_w = preheat;
    ext.tariff_w = oq_ph_v050::clampf(kKp * tariff_shift, -kPr, kPr);
    ext.pv_boost_w = pv;
    ext.window_open = window;
    const auto out = oq_ph_v050::decide_demand_with_extensions(in, tuning, state, ext);
    assert(out.valid);
    state = out.next;

    assert_near(out.requested_w, fork_w, 0.05f);
    assert(out.raw_demand == fork.demand_raw);
    assert_near(out.next.comfort_memory_c, fork.memory_c, 1e-5f);
    assert_near(out.next.last_w, fork.last_w, 0.05f);
  }
}

void test_invalid_inputs() {
  oq_power_house::DemandInput in;
  in.now_ms = 1000;
  in.outside_c = NAN;
  in.cold_c = kTc;
  in.zero_power_c = kT0;
  in.rated_w = kPr;
  in.room_c = 20.0f;
  in.setpoint_c = 20.5f;
  in.water_limit_factor = 1.0f;
  const oq_power_house::DemandTuning tuning{0.5f, kKp, kBelow, kAbove, kRise, kFall, kMaxF};
  oq_ph_v050::DemandExtensions ext;
  ext.pv_boost_w = 2000.0f;
  const auto out = oq_ph_v050::decide_demand_with_extensions(in, tuning, {}, ext);
  assert(!out.valid && out.requested_w == 0.0f && out.raw_demand == 0);
}

void test_frequency_context() {
  oq_ph_v050::FrequencyContext ctx;
  assert(ctx.automatic_frequency_hz(true, 2, 3) == -1);  // tabel nog niet gelezen
  ctx.hp1.hz = {0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90};
  ctx.hp1.known = true;
  assert(ctx.automatic_frequency_hz(true, 2, 3) == 49);
  assert(ctx.automatic_frequency_hz(true, 2, 0) == -1);
  assert(ctx.automatic_frequency_hz(true, 2, 11) == -1);
  ctx.hp1.hz[5] = 0;
  assert(ctx.automatic_frequency_hz(true, 2, 5) == -1);
  assert(ctx.automatic_frequency_hz(false, 2, 3) == -1);  // HP2 los

  ctx.level_cap = 6;
  assert(ctx.frequency_allowed(true, 2, 6));
  assert(!ctx.frequency_allowed(true, 2, 7));
  assert(!ctx.frequency_allowed(true, 2, 0));
  ctx.hp1_excluded_mask = static_cast<uint16_t>(1U << 4);
  assert(!ctx.frequency_allowed(true, 2, 4));
  assert(ctx.frequency_allowed(false, 2, 4));
  assert(ctx.performance_variant(true) == oq_odu::Variant::V1_5);
}

void test_candidate_state() {
  oq_ph_v050::CandidateInputs in;
  in.online = true;
  in.now_ms = 1000000;
  in.minimum_off_ms = 240000;

  in.applied_level = 3;
  in.last_stop_ms = in.now_ms - 1000;
  in.start_limit_remaining_ms = 60000;
  auto s = oq_ph_v050::candidate_state(in);
  assert(s.previous_applied_level == 3 && s.minimum_off_ready && oq_hp_candidate::may_serve_candidate(s));

  in.applied_level = 0;
  in.start_limit_remaining_ms = 0;
  in.last_stop_ms = in.now_ms - 100000;
  assert(!oq_ph_v050::candidate_state(in).minimum_off_ready);
  in.last_stop_ms = in.now_ms - 240000;
  s = oq_ph_v050::candidate_state(in);
  assert(s.minimum_off_ready && oq_hp_candidate::may_serve_candidate(s));
  in.last_stop_ms = 0;
  assert(oq_ph_v050::candidate_state(in).minimum_off_ready);

  in.start_limit_remaining_ms = 30000;
  s = oq_ph_v050::candidate_state(in);
  assert(!s.minimum_off_ready && !oq_hp_candidate::may_serve_candidate(s));
  in.start_limit_remaining_ms = 0;

  in.online = false;
  s = oq_ph_v050::candidate_state(in);
  assert(!s.available_for_start && s.link_suspect && !oq_hp_candidate::may_serve_candidate(s));
  in.online = true;

  in.startup_inhibit = true;
  assert(!oq_ph_v050::candidate_state(in).available_for_start);
  in.startup_inhibit = false;

  in.applied_level = 4;
  in.must_stop = true;
  s = oq_ph_v050::candidate_state(in);
  assert(s.must_stop && !oq_hp_candidate::may_serve_candidate(s));

  in.applied_level = -1;
  assert(oq_ph_v050::candidate_state(in).previous_applied_level == 0);
}

void test_supply_and_frost() {
  assert_near(oq_ph_v050::effective_supply_temp_c(0.0f, kPr, NAN), 30.0f, 1e-4f);
  assert_near(oq_ph_v050::effective_supply_temp_c(3510.0f, kPr, NAN), 42.5f, 1e-4f);
  assert_near(oq_ph_v050::effective_supply_temp_c(9000.0f, kPr, NAN), 55.0f, 1e-4f);
  assert_near(oq_ph_v050::effective_supply_temp_c(3510.0f, kPr, 50.0f), 50.0f, 1e-4f);
  assert_near(oq_ph_v050::effective_supply_temp_c(3510.0f, kPr, 40.0f), 42.5f, 1e-4f);
  assert_near(oq_ph_v050::effective_supply_temp_c(3510.0f, NAN, NAN), 35.0f, 1e-4f);

  assert_near(oq_ph_v050::frost_zone_derate(1.5f, 0.15f), 0.85f, 1e-4f);
  assert_near(oq_ph_v050::frost_zone_derate(5.0f, 0.15f), 1.0f - 0.15f * std::exp(-1.0f), 1e-4f);
  assert(oq_ph_v050::frost_zone_derate(7.0f, 0.15f) == 1.0f);
  assert(oq_ph_v050::frost_zone_derate(-11.0f, 0.15f) == 1.0f);
  assert(oq_ph_v050::frost_zone_derate(NAN, 0.15f) == 1.0f);
  assert_near(oq_ph_v050::frost_zone_derate(1.5f, 0.5f), 0.70f, 1e-4f);
  assert(oq_ph_v050::frost_zone_derate(1.5f, 0.0f) == 1.0f);
  assert(oq_ph_v050::frost_zone_derate(1.5f, NAN) == 1.0f);

  assert(oq_ph_v050::thermal_factor(true, 0.764f, 0.85f) == 0.764f);
  assert(oq_ph_v050::thermal_factor(false, 0.764f, 0.85f) == 0.85f);
}

void test_preheat() {
  assert(oq_ph_v050::preheat_floor_w(0.0f, 20.0f, 21.0f, 3.0f, NAN, kT0, kTc, kPr) == 0.0f);
  assert(oq_ph_v050::preheat_floor_w(0.5f, 20.0f, NAN, 3.0f, NAN, kT0, kTc, kPr) == 0.0f);
  assert(oq_ph_v050::preheat_floor_w(0.5f, 20.0f, 20.3f, 3.0f, NAN, kT0, kTc, kPr) == 0.0f);
  // x = (16 - 3) / 26 = 0.5 -> 0.5 * 0.4 * 7020 * 0.5 = 702
  assert_near(oq_ph_v050::preheat_floor_w(0.5f, 20.0f, 21.0f, 3.0f, NAN, kT0, kTc, kPr), 702.0f, 0.01f);
  // Met verwachting van -10 °C: x = 1 -> 1404
  assert_near(oq_ph_v050::preheat_floor_w(0.5f, 20.0f, 21.0f, 3.0f, -10.0f, kT0, kTc, kPr), 1404.0f, 0.01f);
  assert(oq_ph_v050::preheat_floor_w(NAN, 20.0f, 21.0f, 3.0f, NAN, kT0, kTc, kPr) == 0.0f);
}

}  // namespace

int main() {
  test_demand_matches_fork();
  test_invalid_inputs();
  test_frequency_context();
  test_candidate_state();
  test_supply_and_frost();
  test_preheat();
  return 0;
}
