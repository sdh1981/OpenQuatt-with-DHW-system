#pragma once

// ============================================================================
// OpenQuatt - stille lezing van de slots 3051-3070 (blad), modbus 3050-3069
// ============================================================================
//
// Zelfde patroon als RuntimeFrequencySnapshotReader in
// oq_odu_runtime_frequency_table.h: een eigen ModbusClientDevice dat op eigen
// ritme leest, los van de pollcyclus van de controller, zonder iets te
// schrijven.
//
// Verschillen met die lezer, en waarom:
//   - eerste lezing 30 s na opstart in plaats van 20 s, zodat de ladderlezing
//     eerst klaar is en de twee niet om dezelfde bus vechten;
//   - elk uur verversen in plaats van elk kwartier: deze waarden horen niet te
//     bewegen, en de enige reden om te herhalen is een power cycle van de ODU;
//   - alleen lezen als de versiepoort openstaat. Op niet-R32-firmware gaat er
//     geen enkel verzoek de bus op.
//
// Na drie mislukte pogingen wacht hij tot de volgende ronde. De teller blijft
// staan, want die is de diagnose.
// ============================================================================

#include <array>
#include <cstdint>
#include <span>

#include "oq_odu_step_ceiling_logic.h"

#include "esphome/components/modbus/modbus.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/core/hal.h"

namespace oq_odu_steps {

class SlotSnapshotReader : public esphome::modbus::ModbusClientDevice {
 public:
  static constexpr uint32_t FIRST_READ_AFTER_MS = 30000UL;
  static constexpr uint32_t REFRESH_MS = 60UL * 60UL * 1000UL;
  static constexpr uint32_t RETRY_MS = 5UL * 60UL * 1000UL;
  static constexpr uint32_t PENDING_TIMEOUT_MS = 30000UL;
  static constexpr uint8_t MAX_FAILURES = 3;

  bool bound_to(const esphome::modbus_controller::ModbusController *controller) const {
    return this->controller_ == controller;
  }

  // allowed = versiepoort open (R32). Staat die dicht, dan bindt hij wel maar
  // vraagt hij niets: geen verkeer naar een unit die deze slots niet zo gebruikt.
  void poll(esphome::modbus_controller::ModbusController *controller, uint32_t now_ms, bool bus_busy, bool allowed) {
    if (controller == nullptr) return;
    if (this->controller_ == nullptr) this->bind_(controller, now_ms);
    if (this->pending_) {
      if (static_cast<uint32_t>(now_ms - this->queued_ms_) < PENDING_TIMEOUT_MS) return;
      // Vangnet, net als bij de ladderlezer: blijft de afsluitende callback uit,
      // dan loskoppelen en later opnieuw.
      this->clear_tx_queue_for_device();
      this->fail_(now_ms);
      return;
    }
    if (!allowed || bus_busy) return;
    if (static_cast<int32_t>(now_ms - this->next_due_ms_) < 0) return;
    if (this->failures_ >= MAX_FAILURES && !this->known_) {
      // Opgehouden met proberen tot de volgende ronde.
      this->next_due_ms_ = now_ms + REFRESH_MS;
      return;
    }
    this->pending_ = true;
    this->queued_ms_ = now_ms;
    if (!this->read_holding_registers(SLOT_START_ADDRESS, SLOT_REGISTER_COUNT)) {
      this->fail_(now_ms);
    }
  }

  // Nu opnieuw lezen in plaats van wachten op de volgende ronde. Gebruikt door
  // de sonde: na een schrijfactie wil je zien wat er werkelijk staat, niet de
  // stand van maximaal een uur geleden.
  void request_refresh(uint32_t now_ms) {
    this->next_due_ms_ = now_ms;
    this->failures_ = 0;
  }

  bool known() const { return this->known_; }
  uint8_t failures() const { return this->failures_; }
  uint32_t updated_ms() const { return this->updated_ms_; }
  std::span<const uint16_t> words() const { return std::span<const uint16_t>(this->words_); }

 protected:
  void on_read_holding_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                 esphome::modbus::ResponseStatus status) override {
    if (!this->pending_ || start_address != SLOT_START_ADDRESS) return;
    this->pending_ = false;
    const uint32_t now_ms = esphome::millis();
    if (!esphome::modbus::succeeded(status) || registers.size() < SLOT_REGISTER_COUNT) {
      this->fail_(now_ms);
      return;
    }
    for (size_t i = 0; i < SLOT_REGISTER_COUNT; ++i) this->words_[i] = registers[i];
    this->known_ = true;
    this->failures_ = 0;
    this->updated_ms_ = now_ms;
    this->next_due_ms_ = now_ms + REFRESH_MS;
  }

  void on_custom_response(std::span<const uint8_t>, std::span<const uint8_t>,
                          esphome::modbus::ResponseStatus) override {
    if (this->pending_) this->fail_(esphome::millis());
  }
  bool on_no_response(std::span<const uint8_t>) override {
    if (this->pending_) this->fail_(esphome::millis());
    return false;
  }
  void on_not_sent(std::span<const uint8_t>) override {
    if (this->pending_) this->fail_(esphome::millis());
  }

 private:
  void bind_(esphome::modbus_controller::ModbusController *controller, uint32_t now_ms) {
    this->controller_ = controller;
    this->set_parent(controller->hub());
    this->set_address(controller->device_address());
    this->next_due_ms_ = now_ms + FIRST_READ_AFTER_MS;
  }

  void fail_(uint32_t now_ms) {
    this->pending_ = false;
    if (this->failures_ < 255) this->failures_++;
    this->next_due_ms_ = now_ms + RETRY_MS;
  }

  esphome::modbus_controller::ModbusController *controller_{nullptr};
  std::array<uint16_t, SLOT_REGISTER_COUNT> words_{};
  bool known_{false};
  bool pending_{false};
  uint8_t failures_{0};
  uint32_t queued_ms_{0};
  uint32_t updated_ms_{0};
  uint32_t next_due_ms_{0};
};

// Eén lezer per HP (0 = HP1, 1 = HP2), zo lang levend als het programma: de hub
// moet zijn callback altijd kunnen afleveren.
inline SlotSnapshotReader &slot_reader(bool hp1) {
  static std::array<SlotSnapshotReader, 2> readers;
  return readers[hp1 ? 0 : 1];
}

inline SlotSnapshotReader *slot_reader_for(const esphome::modbus_controller::ModbusController *controller) {
  for (bool hp1 : {true, false}) {
    if (slot_reader(hp1).bound_to(controller)) return &slot_reader(hp1);
  }
  return nullptr;
}

}  // namespace oq_odu_steps
