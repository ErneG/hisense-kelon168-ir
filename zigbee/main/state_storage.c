#include "state_storage.h"

#include <string.h>

#include "esp_check.h"
#include "nvs.h"

static const char *TAG = "state_storage";
static const char *NAMESPACE_NAME = "hisense_ac";
static const char *KEY = "state";

#define HAC_STORED_VERSION 1U

typedef struct {
    uint8_t version;
    uint8_t size;
    ac_state_t want;
    ac_state_t sent;
} hac_stored_state_t;

esp_err_t hac_state_storage_load(ac_state_t *want, ac_state_t *sent, bool *loaded)
{
    hac_stored_state_t stored;
    size_t size = sizeof(stored);
    nvs_handle_t handle;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(want != NULL && sent != NULL && loaded != NULL, ESP_ERR_INVALID_ARG, TAG, "Args required");
    *loaded = false;

    err = nvs_open(NAMESPACE_NAME, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to open NVS namespace");
    err = nvs_get_blob(handle, KEY, &stored, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to read state");
    if (size != sizeof(stored) || stored.version != HAC_STORED_VERSION || stored.size != (uint8_t)sizeof(ac_state_t)) {
        return ESP_OK;
    }
    (void)ac_state_sanitize(&stored.want);
    (void)ac_state_sanitize(&stored.sent);
    *want = stored.want;
    *sent = stored.sent;
    *loaded = true;
    return ESP_OK;
}

esp_err_t hac_state_storage_save(const ac_state_t *want, const ac_state_t *sent)
{
    hac_stored_state_t stored;
    nvs_handle_t handle;
    esp_err_t err;

    memset(&stored, 0, sizeof(stored));
    stored.version = HAC_STORED_VERSION;
    stored.size = (uint8_t)sizeof(ac_state_t);
    stored.want = *want;
    stored.sent = *sent;

    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE_NAME, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace");
    err = nvs_set_blob(handle, KEY, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t hac_state_storage_clear_all(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE_NAME, NVS_READWRITE, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "Failed to open NVS namespace");
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
