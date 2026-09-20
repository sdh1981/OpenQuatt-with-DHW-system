#pragma once

// ============================================================================
// OpenQuatt - welke Power House-motor stuurt
// ============================================================================
//
// Twee motoren rekenen altijd allebei: de fork-lus in oq_power_house_strategy.yaml
// en de v0.50-lus in oq_power_house_v050_shadow.yaml. Dat moet ook, want de
// v0.32-uitbreidingen (zon-EMA, tariefslew, PV-slew, raamdetectie) worden in de
// fork-lus bijgewerkt en door de v0.50-adapter gelezen.
//
// Er schrijft er maar één. Dit bestand houdt bij wie dat is, waarom de keuze van
// de gebruiker eventueel niet gevolgd wordt, en begrenst de eerste minuten na een
// wissel. Bewust vrij van ESPHome, zodat het host-testbaar is
// (tests/host/oq_power_house_engine_test.cpp).
//
// Wat hier NIET in zit: de beschermingen. Low-flow, watertemperatuur, druk en
// persgas grijpen verderop in de keten in, op de aanvraag die hier uitkomt. De
// begrenzing hieronder kan een beschermingsstop dus niet tegenhouden.
// ============================================================================

#include <cstdint>

namespace oq_ph_engine {

inline constexpr uint8_t FORK = 0;
inline constexpr uint8_t V050 = 1;

// Waarom de gekozen motor niet of anders stuurt. OK = hij stuurt gewoon.
inline constexpr uint8_t OK = 0;
inline constexpr uint8_t TABLE_UNKNOWN = 1;     // frequentietabel niet gelezen: Hz-model is blind
inline constexpr uint8_t MODEL_HOLD = 2;        // prestatiemodel even onbruikbaar; standen worden vastgehouden
inline constexpr uint8_t HANDOVER_PENDING = 3;  // defrost of oliehold: nu niet wisselen
inline constexpr uint8_t STRATEGY_INACTIVE = 4; // Power House draait niet (koelen, DHW)
inline constexpr uint8_t WATCHDOG = 5;          // v0.50-lus zwijgt; fork heeft overgenomen

// Gelijk aan upstream's topologie-hold: drie minuten waarin een wissel geen
// compressor start of stopt.
inline constexpr uint32_t kSettleMs = 180000UL;
// Zwijgt de v0.50-lus langer dan dit, dan neemt de fork het over. Zonder deze
// waakhond zou een vastgelopen lus de laatste standen eeuwig laten staan.
inline constexpr uint32_t kStaleMs = 60000UL;

struct State {
  uint8_t owner{FORK};
  uint8_t fallback{OK};
  bool initialized{false};
  bool settle_armed{false};
  uint32_t owner_since_ms{0};
  uint32_t heartbeat_ms{0};  // laatste teken van leven van de v0.50-lus
};

inline State& state() {
  static State instance;
  return instance;
}

struct OwnerInput {
  uint32_t now_ms{0};
  uint8_t selected{FORK};          // keuze van de gebruiker
  bool strategy_active{false};     // Power House is de actieve strategie
  bool output_valid{false};        // v0.50 heeft een gerekend besluit (geen hold)
  bool tables_known{false};        // beide frequentietabellen gelezen
  bool handover_blocked{false};    // defrost of oliehold: een wissel uitstellen
  bool compressors_running{false}; // draait er iets dat een wissel kan verstoren
};

// Aangeroepen door de v0.50-lus, elke tik. Dubbele rol: hartslag voor de
// waakhond en de eigenaarskeuze.
//
// Alleen een onbekende frequentietabel geeft de besturing terug aan de fork.
// Een tijdelijk onbruikbaar prestatiemodel NIET: upstream houdt in dat geval de
// draaiende standen vast, en dat is precies wat je wilt. Eromheen wisselen zou
// bij elke stop een wissel opleveren -- beide units stil in hun minimale
// uit-tijd is namelijk genoeg om het model als "niet bruikbaar" te laten gelden.
inline void decide_owner(const OwnerInput& in) {
  State& s = state();
  s.heartbeat_ms = in.now_ms == 0 ? 1U : in.now_ms;

  uint8_t wanted = in.selected;
  uint8_t status = OK;
  if (wanted == V050 && !in.tables_known) {
    wanted = FORK;
    status = TABLE_UNKNOWN;
  } else if (wanted == V050 && !in.output_valid) {
    status = MODEL_HOLD;
  }
  if (!in.strategy_active) status = STRATEGY_INACTIVE;

  if (!s.initialized) {
    s.initialized = true;
    s.owner = wanted;
    s.fallback = status;
    s.settle_armed = false;
    s.owner_since_ms = 0;
    return;
  }

  if (wanted != s.owner) {
    if (in.handover_blocked) {
      // Niet wisselen terwijl een unit ontdooit of olie terughaalt: dan is de
      // standkeuze tijdelijk bijzonder en zegt een wissel niets.
      s.fallback = HANDOVER_PENDING;
      return;
    }
    s.owner = wanted;
    // Alleen een settle-venster als er iets draait dat verstoord kan worden.
    // Anders zou elke wissel bij stilstand -- en de eerste na het opstarten --
    // drie minuten lang elke start tegenhouden.
    s.settle_armed = in.compressors_running;
    s.owner_since_ms = in.now_ms;
  }
  s.fallback = status;
}

// Zit de wissel nog in zijn settle-venster?
inline bool settling(uint32_t now_ms) {
  State& s = state();
  if (!s.settle_armed) return false;
  if (static_cast<uint32_t>(now_ms - s.owner_since_ms) >= kSettleMs) {
    s.settle_armed = false;
    return false;
  }
  return true;
}

// Aangeroepen door beide lussen vlak voor het schrijven. Waar: die lus mag de
// gedeelde globals vullen.
inline bool fork_drives(uint32_t now_ms) {
  const State& s = state();
  if (s.owner != V050) return true;
  if (s.heartbeat_ms == 0) return true;
  return static_cast<uint32_t>(now_ms - s.heartbeat_ms) > kStaleMs;
}

inline bool v050_drives(uint32_t now_ms) { return !fork_drives(now_ms); }

struct Levels {
  int hp1{0};
  int hp2{0};
};

// Begrenzing binnen het settle-venster: geen draaiende unit stoppen, geen
// stilstaande starten, en hooguit één stand per ronde. Buiten het venster gaat
// de aanvraag ongewijzigd door.
inline Levels settle_clamp(uint32_t now_ms, int hp1_applied, int hp2_applied, int hp1_request, int hp2_request) {
  Levels out{hp1_request, hp2_request};
  if (!settling(now_ms)) return out;

  const auto clamp_one = [](int applied, int request) -> int {
    if (applied <= 0) return 0;  // stond stil: nu niet starten
    int level = request;
    if (level > applied + 1) level = applied + 1;
    if (level < applied - 1) level = applied - 1;
    if (level < 1) level = 1;  // draaide: nu niet stoppen
    return level;
  };
  out.hp1 = clamp_one(hp1_applied, hp1_request);
  out.hp2 = clamp_one(hp2_applied, hp2_request);
  return out;
}

inline const char* engine_name(uint8_t engine) { return engine == V050 ? "v0.50" : "fork"; }

inline const char* fallback_name(uint8_t fallback) {
  switch (fallback) {
    case TABLE_UNKNOWN: return "frequentietabel onbekend";
    case MODEL_HOLD: return "model op hold";
    case HANDOVER_PENDING: return "omschakeling in behandeling";
    case STRATEGY_INACTIVE: return "Power House niet actief";
    case WATCHDOG: return "v0.50-lus zwijgt";
    default: return "";
  }
}

}  // namespace oq_ph_engine
