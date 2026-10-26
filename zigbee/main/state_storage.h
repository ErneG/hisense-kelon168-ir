#pragma once

#include <stdbool.h>

#include "ac_logic.h"
#include "esp_err.h"

esp_err_t hac_state_storage_load(ac_state_t *want, ac_state_t *sent, bool *loaded);
esp_err_t hac_state_storage_save(const ac_state_t *want, const ac_state_t *sent);
esp_err_t hac_state_storage_clear_all(void);
