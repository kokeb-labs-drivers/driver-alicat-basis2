/**
 * @file alicat_basis2_minimal_example.cpp
 * @brief The smallest ESP-IDF program: one instrument at its factory settings, poll the flow once a
 * second.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "alicat_basis2.hpp"
#include "esp32_alicat_basis2_port.hpp"

static const char* const kTag = "basis2_minimal";

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "BASIS 2 driver %s", alicat_basis2::GetDriverVersion());
  static Esp32Port port;
  static alicat_basis2::Bus<Esp32Port> bus(port);
  alicat_basis2::Config config{};
  config.unit_id = CONFIG_ALICAT_BASIS2_EXAMPLE_UNIT_ID[0];
  static alicat_basis2::Basis2<Esp32Port> meter(bus, config);

  auto r = meter.EnsureInitialized();
  if (r) {
    r = meter.Begin(); // learns identity, full scale and firmware
  }
  if (!r) {
    ESP_LOGE(kTag, "start-up failed: %s", alicat_basis2::ToString(r.error));
    return;
  }
  for (;;) {
    const auto frame = meter.ReadFrame();
    if (frame) {
      ESP_LOGI(kTag, "%.2f %s, %.2f C", frame.value.flow.ToDouble(),
               meter.GetIdentity().unit_label.chars.data(), frame.value.temperature.ToDouble());
    } else {
      ESP_LOGW(kTag, "read failed: %s", alicat_basis2::ToString(frame.error));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
