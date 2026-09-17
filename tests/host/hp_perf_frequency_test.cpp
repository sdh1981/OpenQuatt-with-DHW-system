// Host-test voor het frequentie-gebaseerde prestatiemodel (V1.5).
// Afgeleid van upstream OpenQuatt v0.50.0 tests/host/hp_perf_frequency_test.cpp;
// de V2-delen zijn weggelaten omdat deze fork alleen V1.5 kent.

#undef NDEBUG
#include <assert.h>

#include <array>
#include <cmath>

// hp_perf_map.h zet in interp_3d twee if's op een regel; dat is oude, gegenereerde
// code die in de firmware zo compileert. Hier alleen die ene waarschuwing uit.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "../../openquatt/includes/v050/performance/hp_perf_frequency.h"
#pragma GCC diagnostic pop

namespace {

struct FakeFrequencyContext {
  int hp1_hz{49};
  int hp2_hz{55};
  bool allowed{true};

  int automatic_frequency_hz(bool hp1, int, int) const { return hp1 ? hp1_hz : hp2_hz; }
  bool frequency_allowed(bool, int, int) const { return allowed; }
};

void assert_equal_or_nan(float actual, float expected) {
  if (std::isnan(expected)) {
    assert(std::isnan(actual));
    return;
  }
  assert(!std::isnan(actual));
  assert(std::fabs(actual - expected) < 0.01f);
}

}  // namespace

int main() {
  // De as is de fabriekstabel verwarmen F1..F10 uit de EEPROM-dumps.
  constexpr std::array<float, 10> factory_heating = {30, 39, 49, 55, 61, 67, 72, 79, 85, 90};
  assert(oq_perf::V1_HEATING_FREQUENCIES_HZ == factory_heating);
  assert(oq_perf::model_frequency_hz(0) == 0.0f);
  assert(oq_perf::model_frequency_hz(1) == 30.0f);
  assert(oq_perf::model_frequency_hz(11) == 90.0f);

  // Met de fabriekstabel rekent het Hz-model exact gelijk aan het standmodel,
  // ook in de geschatte rijen boven +12 °C en bij aanvoer buiten de tabel.
  for (int level = 1; level <= 10; ++level) {
    const float hz = oq_perf::model_frequency_hz(level);
    for (const float ambient : {-15.0f, -7.0f, 2.0f, 7.0f, 12.0f, 17.0f, 20.0f, 25.0f}) {
      for (const float supply : {30.0f, 35.0f, 45.0f, 55.0f, 65.0f}) {
        assert_equal_or_nan(oq_perf::interp_power_th_w_hz(hz, ambient, supply),
                            oq_perf::interp_power_th_w(level, ambient, supply));
        assert_equal_or_nan(oq_perf::interp_cop_hz(hz, ambient, supply), oq_perf::interp_cop(level, ambient, supply));
        assert_equal_or_nan(oq_perf::interp_power_el_w_hz(hz, ambient, supply),
                            oq_perf::interp_power_el_w(level, ambient, supply));
      }
    }
  }

  // Tussen twee standen lineair op Hz: 52 Hz ligt halverwege 49 (F3) en 55 (F4).
  const float f3 = oq_perf::interp_power_th_w(3, 2.0f, 45.0f);
  const float f4 = oq_perf::interp_power_th_w(4, 2.0f, 45.0f);
  assert_equal_or_nan(oq_perf::interp_power_th_w_hz(52.0f, 2.0f, 45.0f), f3 + (f4 - f3) * 0.5f);
  // Buiten de gemeten as: geen voorspelling. 0 Hz is uit.
  assert(std::isnan(oq_perf::interp_power_th_w_hz(29.0f, 2.0f, 45.0f)));
  assert(std::isnan(oq_perf::interp_power_th_w_hz(91.0f, 2.0f, 45.0f)));
  assert(oq_perf::interp_power_th_w_hz(0.0f, 2.0f, 45.0f) == 0.0f);

  // Een verschoven tabel: stand 3 op 52 Hz levert meer dan het standmodel aanneemt.
  FakeFrequencyContext frequency;
  frequency.hp1_hz = 52;
  const auto shifted = oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, true, 3, 2.0f, 45.0f);
  assert(shifted.usable_for_running_optimization());
  assert(shifted.runtime_frequency_hz == 52);
  assert(shifted.performance.pth_w > oq_perf::interp_power_th_w(3, 2.0f, 45.0f));

  // HP2 heeft zijn eigen tabel.
  const auto hp2 = oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, false, 3, 2.0f, 45.0f);
  assert(hp2.runtime_frequency_hz == 55);
  assert_equal_or_nan(hp2.performance.pth_w, oq_perf::interp_power_th_w(4, 2.0f, 45.0f));

  // Stand 0 is altijd bekend en toegestaan, met nul vermogen.
  const auto off = oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, true, 0, 2.0f, 45.0f);
  assert(off.runtime_frequency_known && off.frequency_policy_allowed && off.performance.pth_w == 0.0f);

  // Tabel nog niet gelezen: niet bruikbaar.
  frequency.hp1_hz = -1;
  assert(!oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, true, 3, 2.0f, 45.0f).runtime_frequency_known);

  // Stand verboden door het beleid (dag/stil-maximum, uitgesloten stand).
  frequency.hp1_hz = 49;
  frequency.allowed = false;
  const auto blocked = oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, true, 3, 2.0f, 45.0f);
  assert(blocked.runtime_frequency_known && blocked.performance.available);
  assert(!blocked.frequency_policy_allowed && !blocked.usable_for_running_optimization());

  // Onbekende of V2-variant levert hier niets: V2 is niet meegenomen.
  frequency.allowed = true;
  assert(!oq_perf::predict_candidate(frequency, oq_odu::Variant::UNKNOWN, true, 3, 2.0f, 45.0f).runtime_frequency_known);
  assert(!oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 3, 2.0f, 45.0f)
              .performance.available);

  // Hoge stand bij hoge aanvoer: de meetdata heeft daar geen punt (NAN) en dan
  // is de kandidaat onbruikbaar, niet geextrapoleerd.
  frequency.hp1_hz = 90;
  assert(!oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, true, 10, -7.0f, 55.0f)
              .usable_for_running_optimization());
  return 0;
}
