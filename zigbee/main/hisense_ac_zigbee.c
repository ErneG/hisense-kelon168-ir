#include <inttypes.h>
#include <stdbool.h>

#include "app_config.h"
#include "commissioning.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_zigbee_secur.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "state_storage.h"
#include "zigbee_device.h"

static const char *TAG = "hisense_ac";

static const hac_device_config_t s_device_config = {
    .primary_channel_mask = HAC_PRIMARY_CHANNEL_MASK,
    .min_join_lqi = HAC_MIN_JOIN_LQI,
};

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    hac_commissioning_handle_signal(signal_struct);
}

static void hac_zigbee_task(void *pv_parameters)
{
    esp_zb_cfg_t zigbee_network_config = HAC_ZR_CONFIG();
    esp_zb_ep_list_t *endpoint_list;
    bool factory_reset_requested;

    (void)pv_parameters;

    factory_reset_requested = hac_commissioning_sample_factory_reset_request();
    if (factory_reset_requested) {
        ESP_LOGW(TAG, "Factory reset requested; clearing persisted AC state before runtime init");
        ESP_ERROR_CHECK(hac_state_storage_clear_all());
    }

    ESP_ERROR_CHECK(hac_device_init());
    ESP_ERROR_CHECK(hac_commissioning_init(&s_device_config, factory_reset_requested));

    esp_zb_secur_network_min_join_lqi_set(s_device_config.min_join_lqi);
    ESP_LOGI(TAG, "Router role, join LQI threshold %u, channel mask 0x%08" PRIx32, s_device_config.min_join_lqi,
             s_device_config.primary_channel_mask);

    esp_zb_init(&zigbee_network_config);

    endpoint_list = hac_device_create_endpoint_list();
    ESP_ERROR_CHECK(endpoint_list != NULL ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(esp_zb_device_register(endpoint_list));
    esp_zb_core_action_handler_register(hac_device_action_handler);
    hac_device_log_thermostat_attrs();
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(s_device_config.primary_channel_mask));
    ESP_ERROR_CHECK(esp_zb_start(false));
    ESP_ERROR_CHECK(hac_commissioning_start_reset_monitor());
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_zb_platform_config_t platform_config = {
        .radio_config = HAC_DEFAULT_RADIO_CONFIG(),
        .host_config = HAC_DEFAULT_HOST_CONFIG(),
    };
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));
    xTaskCreate(hac_zigbee_task, "hac_zigbee", 6144, NULL, 5, NULL);
}
