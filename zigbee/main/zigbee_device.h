#pragma once

#include "esp_err.h"
#include "esp_zigbee_core.h"

esp_err_t hac_device_init(void);
esp_zb_ep_list_t *hac_device_create_endpoint_list(void);
void hac_device_log_thermostat_attrs(void);
esp_err_t hac_device_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message);
