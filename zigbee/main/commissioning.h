#pragma once

#include <stdbool.h>

#include "device_config.h"
#include "esp_err.h"
#include "esp_zigbee_core.h"

esp_err_t hac_commissioning_init(const hac_device_config_t *config, bool factory_reset_requested);
esp_err_t hac_commissioning_start_reset_monitor(void);
bool hac_commissioning_sample_factory_reset_request(void);
void hac_commissioning_handle_signal(esp_zb_app_signal_t *signal_struct);
