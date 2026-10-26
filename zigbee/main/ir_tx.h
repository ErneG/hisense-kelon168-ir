#pragma once

#include <stdint.h>

#include "ac_logic.h"
#include "esp_err.h"

esp_err_t hac_ir_init(void);
/* Non-blocking. Builds frames from `want` and queues them with a 400 ms gap. */
esp_err_t hac_ir_enqueue(const ac_state_t *want, const uint8_t *cmds, size_t count);
/* Non-blocking. The IR task saves the state to NVS after the queued frames. */
esp_err_t hac_ir_enqueue_save(const ac_state_t *want, const ac_state_t *sent);
