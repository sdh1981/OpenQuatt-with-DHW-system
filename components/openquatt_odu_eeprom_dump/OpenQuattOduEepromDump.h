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
// De hub geeft antwoorden door als std::span: register-woorden al in host-
// volgorde, ruwe PDU's als bytes.
#include <span>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

#include "PsramBuffer.h"
#include "esphome/components/modbus/modbus.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_odu_eeprom_dump {

using openquatt_common::PsramBuffer;

// Sinds ESPHome 2026.9.0 is dit zelf een apparaat op de Modbus-hub, naast de
// modbus_controller van dezelfde warmtepomp. Tot dan hing de dump zijn lezingen
// als ModbusCommandItem in de wachtrij van die controller; die klasse en
// queue_command() zijn in 2026.9.0 deprecated en verdwijnen in 2027.3.0.
//
// De controller blijft de bron van hub en adres (zie setup()), dus de YAML hoeft
// niet te veranderen. Wat er wel verandert: de hub belooft per geaccepteerd
// verzoek precies een afsluitende callback. Een foutcode van de unit of een
// uitblijvend antwoord komt dus nu echt binnen, in plaats van pas na de
// tijdslimiet.
class OpenQuattOduEepromDump : public Component, public modbus::ModbusClientDevice {
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

  // Terugkoppeling op het ophalen. De schrijver wist al of de hele JSON de deur
  // uit was -- ChunkedJsonWriter::finish() geeft dat terug -- maar dat werd
  // weggegooid. Zonder deze getters weet je na een dump nergens of je het
  // bestand daadwerkelijk binnen hebt.
  //
  // Bewust GEEN "download bezig"-vlag naar buiten: 51 kB is voorbij voordat een
  // entiteit het doorgeeft. Wat je wil weten is of het gelukt is, en dat blijft
  // staan. Wordt teruggezet bij een nieuwe dump, want dan slaat het op een
  // andere momentopname.
  uint16_t download_count() const { return this->download_count_.load(std::memory_order_acquire); }
  uint32_t last_download_epoch() const { return this->last_download_epoch_.load(std::memory_order_acquire); }
  bool last_download_failed() const { return this->last_download_failed_.load(std::memory_order_acquire); }

  StartResult start(bool include_extended_metadata);
  void write_status(httpd_req_t* req) const;
  bool begin_download();
  /// Geeft terug of de volledige JSON verstuurd is; geef dat door aan end_download().
  bool write_download(httpd_req_t* req) const;
  void end_download(bool completed);

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
  // Vangnet. Een uitblijvend antwoord meldt de hub sinds 2026.9.0 zelf via
  // on_no_response(), na send_wait_time. Deze grens vangt alleen nog het geval
  // dat er helemaal niets terugkomt. Een volle pollronde duurt ~2,2s, dus 8s is
  // ruim.
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
  // uint32_t en niet uint64_t: 64-bits atomics zijn op xtensa niet lock-free.
  // Een epoch past tot 2106 ruim in 32 bits.
  std::atomic<uint16_t> download_count_{0};
  std::atomic<uint32_t> last_download_epoch_{0};
  std::atomic<bool> last_download_failed_{false};
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

  // Hub-callbacks. Ze komen uit de loop() van de hub, dus uit dezelfde taak als
  // onze eigen loop(); ze zetten alleen de vlaggen, loop() handelt af.
  void on_read_holding_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                 modbus::ResponseStatus status) override;
  void on_custom_response(std::span<const uint8_t> request_pdu, std::span<const uint8_t> response_pdu,
                          modbus::ResponseStatus status) override;
  bool on_no_response(std::span<const uint8_t> request_pdu) override;
  void on_not_sent(std::span<const uint8_t> request_pdu) override;

  bool available_storage_() const;
  void reset_job_();
  void queue_current_request_();
  void mark_request_failed_();
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
  static void decode_ascii_words_(const uint16_t* words, size_t count, char* output, size_t output_size);
};

}  // namespace openquatt_odu_eeprom_dump
}  // namespace esphome
