#include "commissioning.h"

#include <inttypes.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "double_reset.h"
#include "commissioning_policy.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "state_storage.h"
#include "app_config.h"
#include "zdo/esp_zigbee_zdo_command.h"

static const char *TAG = "commissioning";

static const hac_device_config_t *s_device_config;
static bool s_factory_reset_requested;
static bool s_active_scan_in_progress;

#if HAC_ENABLE_DOUBLE_RESET_FACTORY_RESET
static RTC_NOINIT_ATTR hac_double_reset_state_t s_double_reset_state;
static TaskHandle_t s_double_reset_window_task_handle;
#endif

#if HAC_FACTORY_RESET_GPIO >= 0
static TaskHandle_t s_reset_monitor_task_handle;

static esp_err_t configure_factory_reset_gpio(void)
{
    gpio_config_t reset_gpio_config = {
        .pin_bit_mask = 1ULL << HAC_FACTORY_RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = HAC_FACTORY_RESET_ACTIVE_LEVEL == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = HAC_FACTORY_RESET_ACTIVE_LEVEL == 0 ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&reset_gpio_config);
}

static bool factory_reset_gpio_is_active(void)
{
    return gpio_get_level(HAC_FACTORY_RESET_GPIO) == HAC_FACTORY_RESET_ACTIVE_LEVEL;
}

static bool wait_for_factory_reset_hold(uint32_t hold_ms)
{
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < hold_ms) {
        if (!factory_reset_gpio_is_active()) {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(HAC_FACTORY_RESET_POLL_MS));
        elapsed_ms += HAC_FACTORY_RESET_POLL_MS;
    }

    return true;
}

static void factory_reset_monitor_task(void *pv_parameters)
{
    (void)pv_parameters;

    for (;;) {
        if (factory_reset_gpio_is_active() && wait_for_factory_reset_hold(HAC_FACTORY_RESET_HOLD_MS)) {
            ESP_LOGW(TAG, "Factory reset button held for %u ms; clearing stored state and resetting Zigbee",
                     HAC_FACTORY_RESET_HOLD_MS);
            if (hac_state_storage_clear_all() != ESP_OK) {
                ESP_LOGW(TAG, "Failed to clear stored light state before factory reset");
            }
            esp_zb_factory_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(HAC_FACTORY_RESET_POLL_MS));
    }
}
#endif

static const char *reset_reason_to_string(esp_reset_reason_t reset_reason)
{
    switch (reset_reason) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_JTAG:
        return "jtag";
    case ESP_RST_EFUSE:
        return "efuse";
    case ESP_RST_CPU_LOCKUP:
        return "cpu lockup";
    default:
        return "other";
    }
}

#if HAC_ENABLE_DOUBLE_RESET_FACTORY_RESET
_Static_assert((int)ESP_RST_POWERON == HAC_RST_POWERON, "reset reason mirror");
_Static_assert((int)ESP_RST_USB == HAC_RST_USB, "reset reason mirror");
_Static_assert((int)ESP_RST_JTAG == HAC_RST_JTAG, "reset reason mirror");
_Static_assert((int)ESP_RST_CPU_LOCKUP == HAC_RST_CPU_LOCKUP, "reset reason mirror");

static bool reset_reason_qualifies_for_double_reset(esp_reset_reason_t reset_reason)
{
    return hac_double_reset_reason_qualifies((int)reset_reason);
}

static void clear_double_reset_state(const char *reason)
{
    if (hac_double_reset_is_armed(&s_double_reset_state)) {
        ESP_LOGI(TAG, "Clearing double-reset detector state: %s", reason);
    }
    hac_double_reset_clear(&s_double_reset_state);
}

static void double_reset_window_task(void *pv_parameters)
{
    (void)pv_parameters;

    vTaskDelay(pdMS_TO_TICKS(HAC_DOUBLE_RESET_WINDOW_MS));
    clear_double_reset_state("window elapsed");
    s_double_reset_window_task_handle = NULL;
    vTaskDelete(NULL);
}

static void start_double_reset_window_task(void)
{
    if (s_double_reset_window_task_handle != NULL) {
        return;
    }

    if (xTaskCreate(double_reset_window_task, "double_reset_window", 4096, NULL, 3,
                    &s_double_reset_window_task_handle) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create double-reset window task");
        clear_double_reset_state("window task create failed");
    }
}
#endif

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    esp_err_t err = esp_zb_bdb_start_top_level_commissioning(mode_mask);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start Zigbee commissioning mode 0x%02x: %s", mode_mask, esp_err_to_name(err));
    }
}

#if HAC_ENABLE_ACTIVE_SCAN_DIAGNOSTIC
static void active_scan_complete_cb(esp_zb_zdp_status_t zdo_status, uint8_t count,
                                    esp_zb_network_descriptor_t *nwk_descriptor)
{
    uint8_t index;

    ESP_LOGI(TAG, "Active scan finished with ZDO status 0x%02x, discovered %u network(s)", zdo_status, count);
    for (index = 0; index < count; ++index) {
        const esp_zb_network_descriptor_t *descriptor = &nwk_descriptor[index];

        ESP_LOGI(TAG,
                 "Scan[%u]: PAN ID=0x%04x permit_join=%d channel=%u router_capacity=%d end_device_capacity=%d ext_pan=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                 index,
                 descriptor->short_pan_id,
                 descriptor->permit_joining,
                 descriptor->logic_channel,
                 descriptor->router_capacity,
                 descriptor->end_device_capacity,
                 descriptor->extended_pan_id[7], descriptor->extended_pan_id[6], descriptor->extended_pan_id[5], descriptor->extended_pan_id[4],
                 descriptor->extended_pan_id[3], descriptor->extended_pan_id[2], descriptor->extended_pan_id[1], descriptor->extended_pan_id[0]);
    }

    s_active_scan_in_progress = false;
    ESP_LOGI(TAG, "Starting network steering after active scan");
    bdb_start_top_level_commissioning_cb(ESP_ZB_BDB_MODE_NETWORK_STEERING);
}

static void start_network_scan_then_steering(void)
{
    uint32_t channel_mask = s_device_config != NULL ? s_device_config->primary_channel_mask : 0U;

    if (s_active_scan_in_progress) {
        return;
    }

    s_active_scan_in_progress = true;
    ESP_LOGI(TAG, "Starting active scan on channel mask 0x%08" PRIx32 " before network steering", channel_mask);
    esp_zb_zdo_active_scan_request(channel_mask, HAC_ACTIVE_SCAN_DURATION, active_scan_complete_cb);
}
#endif

esp_err_t hac_commissioning_init(const hac_device_config_t *config, bool factory_reset_requested)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Device config is required");
    s_device_config = config;
    s_factory_reset_requested = factory_reset_requested;
    return ESP_OK;
}

esp_err_t hac_commissioning_start_reset_monitor(void)
{
#if HAC_FACTORY_RESET_GPIO >= 0
    if (s_reset_monitor_task_handle != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(configure_factory_reset_gpio(), TAG, "Failed to configure factory reset GPIO");
    ESP_RETURN_ON_FALSE(xTaskCreate(factory_reset_monitor_task, "reset_monitor", 3072, NULL, 4,
                                    &s_reset_monitor_task_handle) == pdPASS,
                        ESP_FAIL, TAG, "Failed to create factory reset monitor task");
#endif
    return ESP_OK;
}

static bool hac_commissioning_sample_factory_reset_button(void)
{
#if HAC_FACTORY_RESET_GPIO >= 0
    if (!hac_commissioning_should_sample_factory_reset_gpio_at_boot(HAC_FACTORY_RESET_GPIO)) {
        ESP_LOGI(TAG, "Skipping boot-time sample for factory reset GPIO %d; use runtime long-press instead",
                 HAC_FACTORY_RESET_GPIO);
        return false;
    }

    if (configure_factory_reset_gpio() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure factory reset GPIO %d", HAC_FACTORY_RESET_GPIO);
        return false;
    }

    if (!factory_reset_gpio_is_active()) {
        return false;
    }

    ESP_LOGW(TAG, "Factory reset button detected at boot; waiting for %u ms hold", HAC_FACTORY_RESET_HOLD_MS);
    return wait_for_factory_reset_hold(HAC_FACTORY_RESET_HOLD_MS);
#else
    return false;
#endif
}

bool hac_commissioning_sample_factory_reset_request(void)
{
    esp_reset_reason_t reset_reason = esp_reset_reason();

    ESP_LOGI(TAG, "Boot reset reason: %s (%d)", reset_reason_to_string(reset_reason), (int)reset_reason);

    if (hac_commissioning_sample_factory_reset_button()) {
#if HAC_ENABLE_DOUBLE_RESET_FACTORY_RESET
        clear_double_reset_state("dedicated factory reset GPIO asserted");
#endif
        return true;
    }

#if HAC_ENABLE_DOUBLE_RESET_FACTORY_RESET
    if (!reset_reason_qualifies_for_double_reset(reset_reason)) {
        clear_double_reset_state("reset reason does not qualify");
        return false;
    }

    switch (hac_double_reset_arm_or_trigger(&s_double_reset_state)) {
    case HAC_DOUBLE_RESET_ARMED:
        ESP_LOGI(TAG, "Armed double-reset detector for %u ms", HAC_DOUBLE_RESET_WINDOW_MS);
        start_double_reset_window_task();
        return false;
    case HAC_DOUBLE_RESET_TRIGGERED:
        ESP_LOGW(TAG, "Detected double reset; factory reset and rejoin requested");
        return true;
    case HAC_DOUBLE_RESET_NONE:
    default:
        return false;
    }
#else
    return false;
#endif
}

void hac_commissioning_handle_signal(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *signal_pointer;
    esp_zb_app_signal_type_t signal_type;
    esp_err_t err_status;

    if (signal_struct == NULL || signal_struct->p_app_signal == NULL) {
        ESP_LOGW(TAG, "Received empty Zigbee signal");
        return;
    }

    signal_pointer = signal_struct->p_app_signal;
    signal_type = *signal_pointer;
    err_status = signal_struct->esp_err_status;

    switch (signal_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initializing Zigbee stack");
        bdb_start_top_level_commissioning_cb(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status != ESP_OK) {
            ESP_LOGW(TAG, "%s failed with status %s, retrying initialization",
                     esp_zb_zdo_signal_to_string(signal_type), esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            break;
        }

        if (s_factory_reset_requested) {
            ESP_LOGW(TAG, "Factory reset requested on startup; clearing stored light state and Zigbee storage");
            if (hac_state_storage_clear_all() != ESP_OK) {
                ESP_LOGW(TAG, "Failed to clear persisted light state before factory reset");
            }
            esp_zb_factory_reset();
            break;
        }

        {
            bool factory_new = esp_zb_bdb_is_factory_new();
            bool joined = esp_zb_bdb_dev_joined();

            ESP_LOGI(TAG, "Device started in%s factory-new mode (joined=%d)", factory_new ? "" : " non", joined);
            if (hac_commissioning_should_start_network_steering(factory_new, joined)) {
                ESP_LOGI(TAG, "Starting network steering on channel mask 0x%08" PRIx32,
                         s_device_config != NULL ? s_device_config->primary_channel_mask : 0U);
#if HAC_ENABLE_ACTIVE_SCAN_DIAGNOSTIC
                start_network_scan_then_steering();
#else
                bdb_start_top_level_commissioning_cb(ESP_ZB_BDB_MODE_NETWORK_STEERING);
#endif
            }
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;

            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG,
                     "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel: %d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering failed with status %s, retrying", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            uint8_t permit_join_seconds = 0U;
            void *signal_params = esp_zb_app_signal_get_params(signal_pointer);

            if (signal_params != NULL) {
                permit_join_seconds = *(uint8_t *)signal_params;
            }
            if (permit_join_seconds > 0U) {
                ESP_LOGI(TAG, "Network 0x%04hx is open for %u seconds", esp_zb_get_pan_id(), permit_join_seconds);
            } else {
                ESP_LOGW(TAG, "Network 0x%04hx is closed; joining is not allowed", esp_zb_get_pan_id());
            }
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(signal_type), signal_type, esp_err_to_name(err_status));
        break;
    }
}
