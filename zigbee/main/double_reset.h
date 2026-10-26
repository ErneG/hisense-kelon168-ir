#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t armed;
    uint8_t reserved[2];
} hac_double_reset_state_t;

typedef enum {
    HAC_DOUBLE_RESET_NONE = 0,
    HAC_DOUBLE_RESET_ARMED,
    HAC_DOUBLE_RESET_TRIGGERED,
} hac_double_reset_result_t;

/* Same numbers as esp_reset_reason_t (ESP-IDF v5.5). commissioning.c asserts this at compile time. */
typedef enum {
    HAC_RST_UNKNOWN = 0,
    HAC_RST_POWERON = 1,
    HAC_RST_EXT = 2,
    HAC_RST_SW = 3,
    HAC_RST_PANIC = 4,
    HAC_RST_INT_WDT = 5,
    HAC_RST_TASK_WDT = 6,
    HAC_RST_WDT = 7,
    HAC_RST_DEEPSLEEP = 8,
    HAC_RST_BROWNOUT = 9,
    HAC_RST_SDIO = 10,
    HAC_RST_USB = 11,
    HAC_RST_JTAG = 12,
    HAC_RST_EFUSE = 13,
    HAC_RST_PWR_GLITCH = 14,
    HAC_RST_CPU_LOCKUP = 15,
} hac_reset_reason_t;

/*
 * True when a reset with this reason counts toward the double-reset factory reset.
 * USB resets (USB-Serial-JTAG port open by esptool/pyserial) and JTAG resets do not count,
 * and neither do crash, watchdog, brownout or deep-sleep resets.
 */
bool hac_double_reset_reason_qualifies(int reason);

hac_double_reset_result_t hac_double_reset_arm_or_trigger(hac_double_reset_state_t *state);
void hac_double_reset_clear(hac_double_reset_state_t *state);
bool hac_double_reset_is_armed(const hac_double_reset_state_t *state);
