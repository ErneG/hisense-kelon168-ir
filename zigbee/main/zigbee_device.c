#include "zigbee_device.h"

#include <string.h>

#include "ac_logic.h"
#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "ir_tx.h"
#include "state_storage.h"
#include "zcl_utility.h"

static const char *TAG = "zigbee_device";

static ac_state_t s_want;
static ac_state_t s_sent;
static bool s_sync_pending;

/* Backing storage for ZCL attribute init values. The stack copies them at creation. */
static int16_t s_attr_local_temp = (int16_t)0x8000;
static uint8_t s_attr_resend = AC_RESEND_NONE;

esp_err_t hac_device_init(void)
{
    bool loaded = false;

    ac_state_default(&s_want);
    ac_state_default(&s_sent);
    ESP_RETURN_ON_ERROR(hac_state_storage_load(&s_want, &s_sent, &loaded), TAG, "Load state");
    ESP_LOGI(TAG, "State %s: power=%d mode=%d heat=%u cool=%u fan=%d turbo=%d eco=%d sleep=%d v=%d h=%d disp=%d",
             loaded ? "restored from NVS" : "defaults", s_want.power, s_want.mode, s_want.heat_sp, s_want.cool_sp,
             s_want.fan, s_want.turbo, s_want.economy, s_want.sleep, s_want.vswing, s_want.hswing, s_want.display);
    return hac_ir_init();
}

static esp_err_t add_custom_attr(esp_zb_attribute_list_t *list, uint16_t id, uint8_t type, void *value)
{
    return esp_zb_custom_cluster_add_custom_attr(list, id, type,
                                                 ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                                 value);
}

static void set_thermostat_limit(esp_zb_attribute_list_t *list, uint16_t id, void *value)
{
    esp_err_t err = esp_zb_cluster_update_attr(list, id, value);

    if (err == ESP_ERR_NOT_FOUND) {
        err = esp_zb_thermostat_cluster_add_attr(list, id, value);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Thermostat limit 0x%04x not set: %s", id, esp_err_to_name(err));
    }
}

static void open_setpoint_limits(esp_zb_attribute_list_t *list)
{
    int16_t min_c = HAC_SETPOINT_MIN_CENTI;
    int16_t max_c = HAC_SETPOINT_MAX_CENTI;
    int8_t dead_band = 0;

    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MIN_HEAT_SETPOINT_LIMIT_ID, &min_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MAX_HEAT_SETPOINT_LIMIT_ID, &max_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MIN_COOL_SETPOINT_LIMIT_ID, &min_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MAX_COOL_SETPOINT_LIMIT_ID, &max_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT_ID, &min_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT_ID, &max_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT_ID, &min_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT_ID, &max_c);
    set_thermostat_limit(list, ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_SETPOINT_DEAD_BAND_ID, &dead_band);
}

esp_zb_ep_list_t *hac_device_create_endpoint_list(void)
{
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x01,
    };
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    esp_zb_thermostat_cluster_cfg_t thermostat_cfg = {
        .local_temperature = s_attr_local_temp,
        .occupied_cooling_setpoint = ac_temp_to_centi(s_want.cool_sp),
        .occupied_heating_setpoint = ac_temp_to_centi(s_want.heat_sp),
        .control_sequence_of_operation = 0x04,
        .system_mode = ac_sysmode_to_zcl(&s_want),
    };
    esp_zb_fan_control_cluster_cfg_t fan_cfg = {
        .fan_mode = ac_fan_to_zcl(s_want.fan),
        .fan_mode_sequence = 0x02,
    };
    uint8_t fan_speed = (uint8_t)s_want.fan;
    bool turbo = s_want.turbo;
    bool economy = s_want.economy;
    bool sleep = s_want.sleep;
    bool vswing = s_want.vswing;
    bool hswing = s_want.hswing;
    bool display = s_want.display;
    int16_t heat_sp = ac_temp_to_centi(s_want.heat_sp);
    int16_t cool_sp = ac_temp_to_centi(s_want.cool_sp);
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    esp_zb_attribute_list_t *thermostat = esp_zb_thermostat_cluster_create(&thermostat_cfg);
    esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(HAC_CLUSTER_ID);
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = HAC_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID,
        .app_device_version = 0,
    };
    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = HAC_MANUFACTURER_NAME,
        .model_identifier = HAC_MODEL_IDENTIFIER,
    };

    if (clusters == NULL || thermostat == NULL || custom == NULL || ep_list == NULL) {
        ESP_LOGE(TAG, "Out of memory creating lists");
        return NULL;
    }

    open_setpoint_limits(thermostat);

    if (esp_zb_cluster_list_add_basic_cluster(clusters, esp_zb_basic_cluster_create(&basic_cfg),
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK ||
        esp_zb_cluster_list_add_identify_cluster(clusters, esp_zb_identify_cluster_create(&identify_cfg),
                                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK ||
        esp_zb_cluster_list_add_thermostat_cluster(clusters, thermostat, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK ||
        esp_zb_cluster_list_add_fan_control_cluster(clusters, esp_zb_fan_control_cluster_create(&fan_cfg),
                                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add standard clusters");
        return NULL;
    }

    if (add_custom_attr(custom, HAC_ATTR_FAN_SPEED, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM, &fan_speed) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_TURBO, ESP_ZB_ZCL_ATTR_TYPE_BOOL, &turbo) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_ECONOMY, ESP_ZB_ZCL_ATTR_TYPE_BOOL, &economy) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_SLEEP, ESP_ZB_ZCL_ATTR_TYPE_BOOL, &sleep) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_VSWING, ESP_ZB_ZCL_ATTR_TYPE_BOOL, &vswing) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_HSWING, ESP_ZB_ZCL_ATTR_TYPE_BOOL, &hswing) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_DISPLAY, ESP_ZB_ZCL_ATTR_TYPE_BOOL, &display) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_RESEND, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM, &s_attr_resend) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_HEAT_SETPOINT, ESP_ZB_ZCL_ATTR_TYPE_S16, &heat_sp) != ESP_OK ||
        add_custom_attr(custom, HAC_ATTR_COOL_SETPOINT, ESP_ZB_ZCL_ATTR_TYPE_S16, &cool_sp) != ESP_OK ||
        esp_zb_cluster_list_add_custom_cluster(clusters, custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add custom cluster 0x%04x", HAC_CLUSTER_ID);
        return NULL;
    }

    if (esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add endpoint %u", HAC_ENDPOINT);
        return NULL;
    }
    if (esp_zcl_utility_add_ep_basic_manufacturer_info(ep_list, HAC_ENDPOINT, &info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add manufacturer info");
        return NULL;
    }
    return ep_list;
}

static void set_attr(uint16_t cluster, uint16_t attr, void *value)
{
    esp_zb_zcl_status_t st = esp_zb_zcl_set_attribute_val(HAC_ENDPOINT, cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr,
                                                          value, false);

    if (st != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Sync cluster 0x%04x attr 0x%04x failed: 0x%02x", cluster, attr, st);
    }
}

/* Runs in the Zigbee task through the scheduler, after the write completes. */
static void sync_attributes_cb(uint8_t param)
{
    int16_t heat = ac_temp_to_centi(s_want.heat_sp);
    int16_t cool = ac_temp_to_centi(s_want.cool_sp);
    uint8_t sysmode = ac_sysmode_to_zcl(&s_want);
    uint8_t fan_mode = ac_fan_to_zcl(s_want.fan);
    uint8_t fan_speed = (uint8_t)s_want.fan;
    bool turbo = s_want.turbo;
    bool economy = s_want.economy;
    bool sleep = s_want.sleep;
    bool vswing = s_want.vswing;
    bool hswing = s_want.hswing;
    bool display = s_want.display;
    uint8_t resend = AC_RESEND_NONE;

    (void)param;
    s_sync_pending = false;
    set_attr(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT, ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID, &sysmode);
    set_attr(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT, ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID, &heat);
    set_attr(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT, ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID, &cool);
    set_attr(ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL, ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID, &fan_mode);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_FAN_SPEED, &fan_speed);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_TURBO, &turbo);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_ECONOMY, &economy);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_SLEEP, &sleep);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_VSWING, &vswing);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_HSWING, &hswing);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_DISPLAY, &display);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_RESEND, &resend);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_HEAT_SETPOINT, &heat);
    set_attr(HAC_CLUSTER_ID, HAC_ATTR_COOL_SETPOINT, &cool);
}

static bool read_back_s16(uint16_t attr_id, int16_t *out)
{
    esp_zb_zcl_attr_t *a = esp_zb_zcl_get_attribute(HAC_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id);

    if (a == NULL || a->data_p == NULL) {
        return false;
    }
    if (attr_id == ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_SETPOINT_DEAD_BAND_ID) {
        *out = *(const int8_t *)a->data_p;
    } else {
        memcpy(out, a->data_p, sizeof(*out));
    }
    return true;
}

void hac_device_log_thermostat_attrs(void)
{
    static const uint16_t ids[] = {
        ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MIN_HEAT_SETPOINT_LIMIT_ID, ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MAX_HEAT_SETPOINT_LIMIT_ID,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MIN_COOL_SETPOINT_LIMIT_ID, ESP_ZB_ZCL_ATTR_THERMOSTAT_ABS_MAX_COOL_SETPOINT_LIMIT_ID,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID,   ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT_ID,     ESP_ZB_ZCL_ATTR_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT_ID,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT_ID,     ESP_ZB_ZCL_ATTR_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT_ID,
        ESP_ZB_ZCL_ATTR_THERMOSTAT_MIN_SETPOINT_DEAD_BAND_ID,
    };

    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        int16_t v;

        if (read_back_s16(ids[i], &v)) {
            ESP_LOGI(TAG, "Thermostat attr 0x%04x = %d", ids[i], v);
        } else {
            ESP_LOGW(TAG, "Thermostat attr 0x%04x missing", ids[i]);
        }
    }
    ESP_LOGI(TAG, "Setpoint writes go to cluster 0x%04x attrs 0x%04x/0x%04x (stack enforces cool >= heat on 0x0201)",
             HAC_CLUSTER_ID, HAC_ATTR_HEAT_SETPOINT, HAC_ATTR_COOL_SETPOINT);
}

static void schedule_sync(void)
{
    if (!s_sync_pending) {
        s_sync_pending = true;
        esp_zb_scheduler_alarm(sync_attributes_cb, 0, 20);
    }
}

static void commit(const uint8_t *cmds, size_t count, const ac_state_t *applied)
{
    esp_err_t err = ESP_OK;

    if (count > 0U) {
        err = hac_ir_enqueue(&s_want, cmds, count);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IR enqueue failed (%s); unit state not updated", esp_err_to_name(err));
    } else {
        s_sent = *applied;
    }
    if (hac_ir_enqueue_save(&s_want, &s_sent) != ESP_OK) {
        ESP_LOGW(TAG, "State save not queued");
    }
    schedule_sync();
}

static bool read_bool(const esp_zb_zcl_attribute_t *a, bool *out)
{
    if (a->data.value == NULL || a->data.type != ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
        return false;
    }
    *out = *(const uint8_t *)a->data.value != 0U;
    return true;
}

static bool read_u8(const esp_zb_zcl_attribute_t *a, uint8_t *out)
{
    if (a->data.value == NULL || a->data.size < 1U) {
        return false;
    }
    *out = *(const uint8_t *)a->data.value;
    return true;
}

static bool read_s16(const esp_zb_zcl_attribute_t *a, int16_t *out)
{
    if (a->data.value == NULL || a->data.size < 2U) {
        return false;
    }
    memcpy(out, a->data.value, sizeof(*out));
    return true;
}

static bool handle_thermostat(const esp_zb_zcl_attribute_t *a)
{
    int16_t centi;
    uint8_t u8;
    bool power;
    hs_mode_t mode = s_want.mode;

    switch (a->id) {
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID:
        if (!read_u8(a, &u8) || !ac_sysmode_from_zcl(u8, &power, &mode)) {
            ESP_LOGW(TAG, "Unsupported system_mode %u", u8);
            return true;
        }
        ac_set_power_mode(&s_want, power, mode);
        return true;
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID:
        if (read_s16(a, &centi)) {
            ac_set_setpoint(&s_want, true, ac_temp_from_centi(centi));
        }
        return true;
    case ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID:
        if (read_s16(a, &centi)) {
            ac_set_setpoint(&s_want, false, ac_temp_from_centi(centi));
        }
        return true;
    default:
        return false;
    }
}

static bool handle_custom(const esp_zb_zcl_attribute_t *a, ac_resend_t *resend)
{
    bool on;
    uint8_t u8;
    int16_t centi;

    switch (a->id) {
    case HAC_ATTR_FAN_SPEED:
        if (read_u8(a, &u8) && u8 <= (uint8_t)AC_FAN_HIGH) {
            ac_set_fan(&s_want, (ac_fan_t)u8);
        }
        return true;
    case HAC_ATTR_TURBO:
        if (read_bool(a, &on)) {
            ac_set_turbo(&s_want, on);
        }
        return true;
    case HAC_ATTR_ECONOMY:
        if (read_bool(a, &on)) {
            ac_set_economy(&s_want, on);
        }
        return true;
    case HAC_ATTR_SLEEP:
        if (read_bool(a, &on)) {
            ac_set_sleep(&s_want, on);
        }
        return true;
    case HAC_ATTR_VSWING:
        if (read_bool(a, &on)) {
            ac_set_toggle(&s_want, AC_TOGGLE_VSWING, on);
        }
        return true;
    case HAC_ATTR_HSWING:
        if (read_bool(a, &on)) {
            ac_set_toggle(&s_want, AC_TOGGLE_HSWING, on);
        }
        return true;
    case HAC_ATTR_DISPLAY:
        if (read_bool(a, &on)) {
            ac_set_toggle(&s_want, AC_TOGGLE_DISPLAY, on);
        }
        return true;
    case HAC_ATTR_RESEND:
        if (read_u8(a, &u8)) {
            *resend = (ac_resend_t)u8;
        }
        return true;
    case HAC_ATTR_HEAT_SETPOINT:
    case HAC_ATTR_COOL_SETPOINT:
        if (read_s16(a, &centi)) {
            ac_set_setpoint(&s_want, a->id == HAC_ATTR_HEAT_SETPOINT, ac_temp_from_centi(centi));
        }
        return true;
    default:
        return false;
    }
}

static esp_err_t handle_set_attr(const esp_zb_zcl_set_attr_value_message_t *m)
{
    bool handled = false;
    ac_resend_t resend = AC_RESEND_NONE;
    uint8_t cmds[AC_MAX_CMDS];
    ac_state_t applied;
    size_t count;

    ESP_RETURN_ON_FALSE(m != NULL, ESP_ERR_INVALID_ARG, TAG, "Empty message");
    if (m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS || m->info.dst_endpoint != HAC_ENDPOINT) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Write cluster=0x%04x attr=0x%04x type=0x%02x", m->info.cluster, m->attribute.id,
             m->attribute.data.type);

    switch (m->info.cluster) {
    case ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT:
        handled = handle_thermostat(&m->attribute);
        break;
    case ESP_ZB_ZCL_CLUSTER_ID_FAN_CONTROL:
        if (m->attribute.id == ESP_ZB_ZCL_ATTR_FAN_CONTROL_FAN_MODE_ID) {
            uint8_t zcl = 0U;
            ac_fan_t fan;

            if (read_u8(&m->attribute, &zcl) && ac_fan_from_zcl(zcl, &fan)) {
                ac_set_fan(&s_want, fan);
            }
            handled = true;
        }
        break;
    case HAC_CLUSTER_ID:
        handled = handle_custom(&m->attribute, &resend);
        break;
    default:
        break;
    }

    if (!handled) {
        return ESP_OK;
    }
    if (resend != AC_RESEND_NONE) {
        count = ac_plan_resend(&s_sent, &s_want, resend, cmds, &applied);
        ESP_LOGI(TAG, "Resend action %d: %u frame(s)", resend, (unsigned)count);
    } else {
        count = ac_plan(&s_sent, &s_want, cmds, &applied);
        ESP_LOGI(TAG, "Diff: %u frame(s) to send", (unsigned)count);
    }
    commit(cmds, count, &applied);
    return ESP_OK;
}

esp_err_t hac_device_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return handle_set_attr((const esp_zb_zcl_set_attr_value_message_t *)message);
    default:
        ESP_LOGD(TAG, "Unhandled Zigbee action callback 0x%x", callback_id);
        return ESP_OK;
    }
}
