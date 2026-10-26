#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hisense_codec.h"

#define AC_TEMP_MIN 16U
#define AC_TEMP_MAX 30U
#define AC_TURBO_HEAT_TEMP 30U
#define AC_MAX_CMDS 10U

/* Zigbee-facing fan speed order (custom attribute value). Not the codec order. */
typedef enum {
    AC_FAN_AUTO = 0,
    AC_FAN_LOW = 1,
    AC_FAN_MID_LOW = 2,
    AC_FAN_MID = 3,
    AC_FAN_MID_HIGH = 4,
    AC_FAN_HIGH = 5,
} ac_fan_t;

typedef enum {
    AC_TOGGLE_VSWING = 0,
    AC_TOGGLE_HSWING,
    AC_TOGGLE_DISPLAY,
} ac_toggle_t;

typedef enum {
    AC_RESEND_NONE = 0,
    AC_RESEND_STATE = 1,
    AC_RESEND_VSWING_PULSE = 2,
    AC_RESEND_HSWING_PULSE = 3,
    AC_RESEND_DISPLAY_PULSE = 4,
} ac_resend_t;

typedef struct {
    bool power;
    hs_mode_t mode;
    uint8_t heat_sp;
    uint8_t cool_sp;
    ac_fan_t fan;
    bool sleep;
    bool turbo;
    bool economy;
    bool vswing;
    bool hswing;
    bool display;
    bool fan_stash_valid;
    ac_fan_t fan_stash;
    bool temp_stash_valid;
    uint8_t temp_stash;
} ac_state_t;

void ac_state_default(ac_state_t *s);
/* Clamp every field into range. Returns false if anything had to be fixed. */
bool ac_state_sanitize(ac_state_t *s);
uint8_t ac_active_temp(const ac_state_t *s);
hs_fan_t ac_fan_to_hs(ac_fan_t fan);
void ac_to_hs(const ac_state_t *s, hs_state_t *out);

/* Setters apply the remote's side effects (ported from flipper/hisense_ac/hisense_ac.c). */
void ac_set_power_mode(ac_state_t *s, bool power, hs_mode_t mode);
void ac_set_power(ac_state_t *s, bool power);
void ac_set_setpoint(ac_state_t *s, bool heating, uint8_t temp);
/* True when the single climate setpoint maps to the heating setpoint: ZCL off and heat. Cool, dry, fan_only use cooling. */
bool ac_zcl_sysmode_uses_heating(uint8_t zcl_mode);
void ac_set_fan(ac_state_t *s, ac_fan_t fan);
void ac_set_sleep(ac_state_t *s, bool on);
void ac_set_economy(ac_state_t *s, bool on);
void ac_set_turbo(ac_state_t *s, bool on);
void ac_set_toggle(ac_state_t *s, ac_toggle_t which, bool on);

/*
 * Compare what the unit has (sent) with what Zigbee wants (want).
 * Writes the IR commands to send, in order, and the state the unit has after them.
 * Order: power, mode, temp, fan, turbo, economy, sleep, vswing, hswing, display.
 * Each frame is built from `want` with hs_build_frame.
 */
size_t ac_plan(const ac_state_t *sent, const ac_state_t *want, uint8_t cmds[AC_MAX_CMDS], ac_state_t *applied);
size_t ac_plan_resend(const ac_state_t *sent, const ac_state_t *want, ac_resend_t action,
                      uint8_t cmds[AC_MAX_CMDS], ac_state_t *applied);

/* ZCL mapping helpers. */
#define AC_ZCL_SYSMODE_OFF 0x00U
#define AC_ZCL_SYSMODE_COOL 0x03U
#define AC_ZCL_SYSMODE_HEAT 0x04U
#define AC_ZCL_SYSMODE_FAN_ONLY 0x07U
#define AC_ZCL_SYSMODE_DRY 0x08U

#define AC_ZCL_FANMODE_LOW 0x01U
#define AC_ZCL_FANMODE_MEDIUM 0x02U
#define AC_ZCL_FANMODE_HIGH 0x03U
#define AC_ZCL_FANMODE_AUTO 0x05U

uint8_t ac_sysmode_to_zcl(const ac_state_t *s);
bool ac_sysmode_from_zcl(uint8_t zcl, bool *power, hs_mode_t *mode);
uint8_t ac_fan_to_zcl(ac_fan_t fan);
bool ac_fan_from_zcl(uint8_t zcl, ac_fan_t *fan);
int16_t ac_temp_to_centi(uint8_t temp);

/* Uppercase hex, no separators, NUL-terminated. Needs out_size >= 2 * len + 1, else writes "" and returns false. */
#define AC_FRAME_HEX_SIZE (2U * HS_FRAME_BYTES + 1U)
bool ac_bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_size);
uint8_t ac_temp_from_centi(int16_t centi);
