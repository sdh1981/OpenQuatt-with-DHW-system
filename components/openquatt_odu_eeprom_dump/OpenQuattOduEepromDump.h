#pragma once

// =============================================================================
// ODU EEPROM dump
// =============================================================================
// Overgenomen uit OpenQuatt/OpenQuatt, branch codex/odu-eeprom-dump (augustus
// 2026). Leest de EEPROM van de buitenunit read-only uit en biedt hem aan op
// drie HTTP-endpoints per warmtepomp.
//
// Aangepast: de afhankelijkheid op openquatt_web_auth is verwijderd. Dat
// component hangt aan de eigen webapp van upstream, die in deze fork sinds
// v0.42 niet meer meegebouwd wordt. Het endpoint is daarmee even open als de
// rest van de webserver hier -- zie de documentatie voor wat dat betekent.
// =============================================================================

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
// ESPHome 2026.8.0 geeft de Modbus-payload door als std::span<const uint8_t>,
// waar dat eerder een const std::vector<uint8_t>& was. Een span converteert niet
// naar een vector, dus de oude vorm compileert daar niet meer. Andersom werkt
// het wel: een span-parameter accepteert op 2026.7.0 gewoon de vector, die
// impliciet converteert. Daarom span, en niet een versie-ifdef.
#include <span>
#include <vector>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

#include "PsramBuffer.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_odu_eeprom_dump {

using openquatt_common::PsramBuffer;

class OpenQuattOduEepromDump : public Component {
 public:
  enum class StartResult : uint8_t { STARTED = 0, BUSY, UNAVAILABLE };

  void set_controller(modbus_controller::ModbusController* controller) { this->controller_ = controller; }
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_hp_index(uint8_t hp_index) { this->hp_index_ = hp_index; }
  void set_device_address(uint8_t device_address) { this->device_address_ = device_address; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool is_active() const { return this->active_.load(std::memory_order_acquire); }

  // Toegevoegd in deze fork. Upstream leest deze toestand uit via de eigen
  // webapp; hier moeten ESPHome-entiteiten erbij kunnen zodat je de dump vanuit
  // Home Assistant of de klassieke web-UI kunt starten en volgen.
  bool is_available() const { return this->available_.load(std::memory_order_acquire); }
  bool has_snapshot() const { return this->dump_ready_.load(std::memory_order_acquire); }
  uint8_t progress_percent() const { return this->progress_.load(std::memory_order_acquire); }
  uint16_t registers_read() const { return this->registers_read_.load(std::memory_order_acquire); }

  StartResult start(bool include_extended_metadata);
  void write_status(httpd_req_t* req) const;
  bool begin_download();
  void write_download(httpd_req_t* req) const;
  void end_download();

 protected:
  static constexpr uint16_t EEPROM_START_ADDRESS = 2999;
  static constexpr uint16_t EEPROM_REGISTER_COUNT = 512;
  static constexpr uint16_t EEPROM_CRC_DATA_COUNT = 510;
  static constexpr uint16_t EEPROM_BLOCK_SIZE = 22;
  static constexpr uint16_t CORE_START_ADDRESS = 2114;
  static constexpr uint16_t CORE_REGISTER_COUNT = 14;
  static constexpr uint16_t EXTENDED_START_ADDRESS = 11004;
  static constexpr uint16_t EXTENDED_REGISTER_COUNT = 6;
  static constexpr uint16_t MODEL_START_ADDRESS = 11120;
  static constexpr uint16_t CUSTOMER_MODEL_START_ADDRESS = 11160;
  static constexpr uint16_t SERIAL_START_ADDRESS = 11219;
  static constexpr uint16_t TEXT_REGISTER_COUNT = 20;
  static constexpr uint32_t FAILURE_COOLDOWN_MS = 1000;
  // Afstand tussen twee verzoeken. Verving de wachtrij-controle die ESPHome
  // 2026.8.0 onmogelijk maakte; zie de toelichting in loop(). 29 verzoeken
  // (5 identiteit + 24 blokken van 22 registers) maakt de dump ~15s lang.
  static constexpr uint32_t REQUEST_SPACING_MS = 500;
  // Was 30s toen een lege wachtrij een verloren antwoord binnen 500ms verried.
  // Die snelle route is weg, dus dit is nu de enige detectie en moet navenant
  // korter: een volle pollronde duurt ~2,2s, dus 8s is ruim en toch snel.
  static constexpr uint32_t REQUEST_TIMEOUT_MS = 8000;

  enum class Step : uint8_t {
    WAITING_BUS = 0,
    EXTENDED,
    MODEL,
    CUSTOMER_MODEL,
    SERIAL,
    CORE,
    EEPROM,
    VERIFYING,
    COMPLETE,
    FAILED,
  };

  enum Warning : uint8_t {
    WARNING_NONE = 0,
    WARNING_EXTENDED_UNAVAILABLE = 1U << 0U,
    WARNING_CORE_UNAVAILABLE = 1U << 1U,
    WARNING_RUNTIME_DIFFERS = 1U << 2U,
  };

  modbus_controller::ModbusController* controller_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  uint8_t hp_index_{0};
  uint8_t device_address_{0};
  PsramBuffer<uint16_t> eeprom_{};
  std::array<uint16_t, EXTENDED_REGISTER_COUNT> extended_{};
  std::array<uint16_t, TEXT_REGISTER_COUNT> model_{};
  std::array<uint16_t, TEXT_REGISTER_COUNT> customer_model_{};
  std::array<uint16_t, TEXT_REGISTER_COUNT> serial_{};
  std::array<uint16_t, CORE_REGISTER_COUNT> core_{};

  std::atomic<bool> available_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> starting_{false};
  std::atomic<bool> dump_ready_{false};
  std::atomic<bool> download_in_progress_{false};
  std::atomic<uint8_t> progress_{0};
  std::atomic<uint16_t> registers_read_{0};
  std::atomic<uint32_t> job_id_{0};
  std::atomic<uint8_t> warning_flags_{WARNING_NONE};
  std::atomic<bool> extended_supported_{false};
  std::atomic<bool> core_available_{false};
  std::atomic<bool> crc_matches_stored_eeprom_{false};
  std::atomic<uint16_t> calculated_crc_{0};
  std::atomic<uint16_t> stored_crc_{0};
  std::atomic<uint8_t> crc_retry_count_{0};
  std::atomic<uint32_t> request_token_{0};

  mutable portMUX_TYPE state_mux_ = portMUX_INITIALIZER_UNLOCKED;
  Step step_{Step::WAITING_BUS};
  bool include_extended_metadata_{true};
  std::atomic<bool> waiting_for_response_{false};
  std::atomic<bool> response_received_{false};
  std::atomic<bool> response_valid_{false};
  uint16_t request_start_address_{0};
  uint16_t request_register_count_{0};
  uint16_t eeprom_offset_{0};
  uint8_t block_retry_count_{0};
  uint32_t started_ms_{0};
  uint32_t completed_ms_{0};
  uint64_t captured_at_epoch_{0};
  uint32_t queued_ms_{0};
  uint32_t next_request_ms_{0};
  char phase_[48]{"idle"};
  char error_[96]{};

  bool available_storage_() const;
  void reset_job_();
  void queue_current_request_();
  void on_response_(uint32_t request_token, uint16_t start_address, std::span<const uint8_t> data);
  void handle_request_result_();
  void handle_request_failure_();
  void advance_after_success_();
  void finish_job_();
  void fail_job_(const char* error);
  void set_phase_(const char* phase);
  void set_error_(const char* error);
  void add_warning_(Warning warning);
  uint16_t current_start_address_() const;
  uint16_t current_register_count_() const;
  uint16_t calculate_crc_() const;
  static uint16_t read_word_(std::span<const uint8_t> data, size_t index);
  static void decode_ascii_words_(const uint16_t* words, size_t count, char* output, size_t output_size);
};

}  // namespace openquatt_odu_eeprom_dump
}  // namespace esphome
