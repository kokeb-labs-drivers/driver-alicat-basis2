/**
 * @file alicat_basis2_full_example.cpp
 * @brief The full ESP-IDF program: scan the line, identify the instrument, use the unsaved setpoint
 * source, take a measurement window and map the flags to a status.
 *
 * Run it with `CONFIG_ALICAT_BASIS2_EXAMPLE_RS485` on a multidrop line. It never calls a guarded
 * command.
 */
#include <cstdint>
#include <string_view>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "alicat_basis2.hpp"
#include "esp32_alicat_basis2_port.hpp"

namespace {

/// The unit IDs in order: bit i of `ScanUnitIds().found` is `kUnitIds[i]`.
constexpr std::string_view kUnitIds = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

const char* const kTag = "basis2_full";

/// The one line a status LED task would show: the manual's LED table, reduced to what a program can
/// read.
const char* StatusText(const alicat_basis2::StatusFlags& flags) {
  if (flags.AnyFault()) {
    return "fault (MOV, TOV or OVR)";
  }
  if (flags.valve_hold || flags.valve_thermal) {
    return "valve override";
  }
  return "ok";
}

} // namespace

extern "C" void app_main(void) {
  static Esp32Port port;
  static alicat_basis2::Bus<Esp32Port> bus(port);
  alicat_basis2::Config config{};
  config.unit_id = CONFIG_ALICAT_BASIS2_EXAMPLE_UNIT_ID[0];
  static alicat_basis2::Basis2<Esp32Port> meter(bus, config);

  auto r = meter.EnsureInitialized();
  if (r) {
    r = meter.Begin();
  }
  if (!r) {
    ESP_LOGE(kTag, "start-up failed: %s", alicat_basis2::ToString(r.error));
    return;
  }

  // 1. Who is on the line? One poll per unit ID; this call blocks for the whole scan.
  const auto scan = meter.ScanUnitIds();
  if (scan) {
    for (uint32_t i = 0U; i < static_cast<uint32_t>(kUnitIds.size()); ++i) {
      const char id = kUnitIds[i]; // bit i of `found` is the i-th unit ID, A to Z
      if ((scan.value.found & (1U << i)) != 0U) {
        ESP_LOGI(kTag, "instrument at unit ID %c", id);
      }
    }
  }

  // 2. Identify the one we talk to.
  const auto& id = meter.GetIdentity();
  ESP_LOGI(kTag, "serial %s, firmware %s, full scale %.1f %s", id.serial.chars.data(),
           id.firmware_text.chars.data(), id.full_scale.ToDouble(), id.unit_label.chars.data());

  // 3. A controller takes setpoints on the unsaved source, which never wears the non-volatile
  // memory.
  if (id.kind == alicat_basis2::DeviceKind::Controller) {
    if (auto s = meter.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital); !s) {
      ESP_LOGE(kTag, "setpoint source: %s", alicat_basis2::ToString(s.error));
    } else if (auto sp = meter.SetSetpoint(id.full_scale.ToDouble() * 0.1); !sp) {
      ESP_LOGE(kTag, "setpoint: %s", alicat_basis2::ToString(sp.error));
    } else {
      // Both calls succeeded.
    }
  }

  // 4. A measurement window: trigger a 2 s average of the flow, then read it back.
  if (auto m = meter.StartMeasurement(2000); m) {
    vTaskDelay(pdMS_TO_TICKS(2100));
    if (const auto avg = meter.ReadAverages(); avg) {
      ESP_LOGI(kTag, "average flow over the window: %.3f", avg.value.flow.ToDouble());
    }
  }

  for (;;) {
    const auto frame = meter.ReadFrame();
    if (frame) {
      ESP_LOGI(kTag, "%.2f %s, gas %s, status: %s", frame.value.flow.ToDouble(),
               id.unit_label.chars.data(), frame.value.gas_name.data(),
               StatusText(frame.value.status));
    } else {
      ESP_LOGW(kTag, "read failed: %s", alicat_basis2::ToString(frame.error));
      static_cast<void>(meter.Recover()); // find the instrument again after an unexplained failure
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
