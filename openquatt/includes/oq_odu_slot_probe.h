#pragma once

// ============================================================================
// OpenQuatt - sonde op de slots 3051-3070: het Modbus-verkeer
// ============================================================================
//
// Eén register per proef, in deze volgorde:
//
//   CONTROLE    2099..2103 lezen; de compressor moet stilstaan
//   VOOR        het slot lezen en onthouden wat erin stond
//   SCHRIJVEN   de nieuwe waarde erin
//   TERUGLEZEN  wat de unit er werkelijk van maakte
//
// Daarna staat de oorspronkelijke waarde klaar om teruggezet te worden, met de
// knop of vanzelf na twee minuten.
//
// Grendels, en waarom:
//   - vrijgaveschakelaar, dooft bij elke herstart;
//   - compressor uit, zonder ontsnapping. De frequentietabel heeft een
//     "tijdens bedrijf"-schakelaar omdat we daar weten wat we schrijven. Hier
//     niet, dus die ontbreekt bewust;
//   - geen schrijfactie tijdens een EEPROM-dump;
//   - alleen bladadres 3051 t/m 3070, waarde 0 t/m 100;
//   - register 5000 (vastleggen naar EEPROM) komt hier niet voor. Alles blijft
//     vluchtig, dus een power cycle van de buitenunit is altijd de weg terug.
// ============================================================================

#include <array>
#include <cstdint>
#include <span>

#include "oq_odu_runtime_frequency_table_logic.h"
#include "oq_odu_slot_probe_logic.h"

#include "esphome/components/modbus/modbus.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace oq_odu_slot_probe {

static const char *const TAG = "oq_slot_probe";

struct SlotProbeRefs {
  esphome::modbus_controller::ModbusController *controller;
  esphome::openquatt_odu_eeprom_dump::OpenQuattOduEepromDump *eeprom_dump;
  esphome::switch_::Switch *enable_switch;
  esphome::text_sensor::TextSensor *status;
  const char *prefix;
};

inline void publish_status(const SlotProbeRefs &refs, const std::string &message) {
  refs.status->publish_state(message);
  ESP_LOGW(TAG, "%s%s", refs.prefix, message.c_str());
}

class SlotProbeDevice : public esphome::modbus::ModbusClientDevice {
 public:
  static constexpr uint32_t STALE_AFTER_MS = 30000;

  bool bound_to(const esphome::modbus_controller::ModbusController *controller) const {
    return this->controller_ == controller;
  }

  void bind(esphome::modbus_controller::ModbusController *controller) {
    this->controller_ = controller;
    this->set_parent(controller->hub());
    this->set_address(controller->device_address());
  }

  const ProbeState &probe_state() const { return this->state_; }

  void start_probe(const SlotProbeRefs &refs, uint32_t sheet_address, uint32_t value) {
    if (!this->claim_(refs)) return;
    if (refs.enable_switch == nullptr || !refs.enable_switch->state) {
      this->finish_(refs, refused_text("schrijven is niet vrijgegeven"));
      return;
    }
    if (!valid_sheet_address(sheet_address)) {
      this->finish_(refs, refused_text("adres valt buiten 3051-3070"));
      return;
    }
    if (!valid_value(value)) {
      this->finish_(refs, refused_text("waarde valt buiten 0-100"));
      return;
    }
    if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
      this->finish_(refs, refused_text("er loopt een EEPROM-dump"));
      return;
    }
    if (this->state_.restore_pending) {
      this->finish_(refs, refused_text("vorige proef staat nog open; eerst herstellen"));
      return;
    }
    this->sheet_address_ = static_cast<uint16_t>(sheet_address);
    this->value_ = static_cast<uint16_t>(value);
    publish_status(refs, std::string("CONTROLE: toestand van de unit wordt gelezen"));
    this->queue_read_(refs, Stage::GUARD, oq_odu_runtime_frequency::GUARD_START_ADDRESS,
                      oq_odu_runtime_frequency::GUARD_REGISTER_COUNT);
  }

  void start_restore(const SlotProbeRefs &refs) {
    if (!this->state_.restore_pending) {
      publish_status(refs, std::string("niets te herstellen"));
      return;
    }
    if (!this->claim_(refs)) return;
    this->sheet_address_ = this->state_.sheet_address;
    this->queue_write_(refs, Stage::RESTORE_WRITE, this->state_.original);
  }

  // Elke seconde aanroepen: zet de oorspronkelijke waarde vanzelf terug.
  void tick(const SlotProbeRefs &refs, uint32_t now_ms) {
    if (this->stage_ != Stage::IDLE) {
      if (static_cast<uint32_t>(now_ms - this->queued_ms_) >= STALE_AFTER_MS) {
        ESP_LOGW(TAG, "%svorige opdracht kwam nooit af; losgekoppeld", refs.prefix);
        this->clear_tx_queue_for_device();
        this->stage_ = Stage::IDLE;
        publish_status(refs, std::string("AFGEBROKEN: de unit antwoordde niet"));
      }
      return;
    }
    if (restore_due(this->state_, now_ms)) this->start_restore(refs);
  }

 protected:
  enum class Stage : uint8_t { IDLE, GUARD, READ_BEFORE, WRITE, READBACK, RESTORE_WRITE, RESTORE_READBACK };

  void on_read_holding_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                 esphome::modbus::ResponseStatus status) override {
    if (this->stage_ == Stage::IDLE || start_address != this->expected_start_) return;
    if (!esphome::modbus::succeeded(status)) {
      this->fail_(status);
      return;
    }
    switch (this->stage_) {
      case Stage::GUARD:
        this->handle_guard_(registers);
        break;
      case Stage::READ_BEFORE:
        this->handle_read_before_(registers);
        break;
      case Stage::READBACK:
        this->handle_readback_(registers);
        break;
      case Stage::RESTORE_READBACK:
        this->handle_restore_readback_(registers);
        break;
      default:
        break;
    }
  }

  void on_write_multiple_registers(uint16_t start_address, std::span<const uint16_t>,
                                   esphome::modbus::ResponseStatus status) override {
    if (start_address != this->expected_start_) return;
    if (this->stage_ == Stage::WRITE) {
      if (!esphome::modbus::succeeded(status)) {
        this->fail_(status);
        return;
      }
      this->queue_read_(this->refs_, Stage::READBACK, modbus_address(this->sheet_address_), 1);
      return;
    }
    if (this->stage_ == Stage::RESTORE_WRITE) {
      if (!esphome::modbus::succeeded(status)) {
        this->fail_(status);
        return;
      }
      this->queue_read_(this->refs_, Stage::RESTORE_READBACK, modbus_address(this->sheet_address_), 1);
    }
  }

  void on_custom_response(std::span<const uint8_t>, std::span<const uint8_t>,
                          esphome::modbus::ResponseStatus) override {
    if (this->stage_ != Stage::IDLE) this->finish_(this->refs_, refused_text("onverwacht antwoord"));
  }
  bool on_no_response(std::span<const uint8_t>) override {
    if (this->stage_ != Stage::IDLE) this->finish_(this->refs_, refused_text("geen antwoord van de unit"));
    return false;
  }
  void on_not_sent(std::span<const uint8_t>) override {
    if (this->stage_ != Stage::IDLE) this->finish_(this->refs_, refused_text("opdracht niet verstuurd"));
  }

 private:
  bool claim_(const SlotProbeRefs &refs) {
    if (this->stage_ != Stage::IDLE) {
      publish_status(refs, std::string("BEZIG: vorige opdracht loopt nog"));
      return false;
    }
    if (refs.controller == nullptr) return false;
    if (!this->bound_to(refs.controller)) this->bind(refs.controller);
    this->refs_ = refs;
    return true;
  }

  void handle_guard_(std::span<const uint16_t> registers) {
    if (registers.size() < oq_odu_runtime_frequency::GUARD_REGISTER_COUNT) {
      this->finish_(this->refs_, refused_text("toestand van de unit onleesbaar"));
      return;
    }
    const uint16_t working_mode = registers[oq_odu_runtime_frequency::GUARD_WORKING_MODE_INDEX];
    const uint16_t hz = registers[oq_odu_runtime_frequency::GUARD_COMPRESSOR_FREQUENCY_INDEX];
    // Bewust zonder "tijdens bedrijf"-ontsnapping: we weten niet wat deze
    // registers doen, dus ze worden niet in een draaiende unit geschreven.
    const auto decision = oq_odu_runtime_frequency::decide_guard(working_mode, hz, false);
    if (decision != oq_odu_runtime_frequency::GuardDecision::ALLOW_STANDSTILL) {
      this->finish_(this->refs_, refused_text("de compressor draait"));
      return;
    }
    this->queue_read_(this->refs_, Stage::READ_BEFORE, modbus_address(this->sheet_address_), 1);
  }

  void handle_read_before_(std::span<const uint16_t> registers) {
    if (registers.empty()) {
      this->finish_(this->refs_, refused_text("slot onleesbaar"));
      return;
    }
    this->original_ = registers[0];
    this->queue_write_(this->refs_, Stage::WRITE, this->value_);
  }

  void handle_readback_(std::span<const uint16_t> registers) {
    if (registers.empty()) {
      this->finish_(this->refs_, refused_text("teruglezen mislukt"));
      return;
    }
    const uint16_t readback = registers[0];
    // Boekhouding voor het herstellen, ook als de waarde niet bleef staan: er is
    // geschreven, dus er moet teruggezet worden.
    this->state_.restore_pending = true;
    this->state_.sheet_address = this->sheet_address_;
    this->state_.original = this->original_;
    this->state_.written = this->value_;
    this->state_.written_ms = esphome::millis();
    this->finish_(this->refs_, result_text(this->sheet_address_, this->original_, this->value_, readback) +
                                   verdict_text(this->value_, readback));
  }

  void handle_restore_readback_(std::span<const uint16_t> registers) {
    const uint16_t readback = registers.empty() ? 0 : registers[0];
    const uint16_t original = this->state_.original;
    this->state_ = ProbeState{};
    this->finish_(this->refs_, restored_text(this->sheet_address_, original, readback));
  }

  void queue_read_(const SlotProbeRefs &refs, Stage stage, uint16_t address, uint16_t count) {
    this->stage_ = stage;
    this->expected_start_ = address;
    this->queued_ms_ = esphome::millis();
    if (!this->read_holding_registers(address, count)) {
      this->finish_(refs, refused_text("lezen niet verstuurd"));
    }
  }

  void queue_write_(const SlotProbeRefs &refs, Stage stage, uint16_t value) {
    this->stage_ = stage;
    this->expected_start_ = modbus_address(this->sheet_address_);
    this->queued_ms_ = esphome::millis();
    this->write_buffer_[0] = value;
    publish_status(refs, std::string(stage == Stage::WRITE ? "SCHRIJVEN" : "HERSTELLEN"));
    if (!this->write_multiple_registers(this->expected_start_,
                                        std::span<const uint16_t>(this->write_buffer_.data(), 1))) {
      this->finish_(refs, refused_text("schrijven niet verstuurd"));
    }
  }

  void fail_(esphome::modbus::ResponseStatus status) {
    char reason[32];
    snprintf(reason, sizeof(reason), "foutcode 0x%02X", static_cast<unsigned>(*status));
    this->finish_(this->refs_, refused_text(reason));
  }

  void finish_(const SlotProbeRefs &refs, const std::string &message) {
    this->stage_ = Stage::IDLE;
    publish_status(refs, message);
  }

  esphome::modbus_controller::ModbusController *controller_{nullptr};
  SlotProbeRefs refs_{};
  ProbeState state_{};
  std::array<uint16_t, 1> write_buffer_{};
  Stage stage_{Stage::IDLE};
  uint16_t sheet_address_{0};
  uint16_t value_{0};
  uint16_t original_{0};
  uint16_t expected_start_{0};
  uint32_t queued_ms_{0};
};

// Eén sonde per HP, zo lang levend als het programma.
inline SlotProbeDevice &slot_probe(bool hp1) {
  static std::array<SlotProbeDevice, 2> devices;
  return devices[hp1 ? 0 : 1];
}

}  // namespace oq_odu_slot_probe
