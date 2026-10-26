#include "ac_logic.h"

static uint8_t clamp_temp(int value)
{
    if (value < (int)AC_TEMP_MIN) {
        return AC_TEMP_MIN;
    }
    if (value > (int)AC_TEMP_MAX) {
        return AC_TEMP_MAX;
    }
    return (uint8_t)value;
}

static bool mode_valid(hs_mode_t mode)
{
    return mode == HsModeHeat || mode == HsModeCool || mode == HsModeDry || mode == HsModeFan;
}

void ac_state_default(ac_state_t *s)
{
    *s = (ac_state_t){
        .power = false,
        .mode = HsModeHeat,
        .heat_sp = 22U,
        .cool_sp = 24U,
        .fan = AC_FAN_AUTO,
    };
}

bool ac_state_sanitize(ac_state_t *s)
{
    bool ok = true;

    if (!mode_valid(s->mode)) {
        s->mode = HsModeHeat;
        ok = false;
    }
    if (s->heat_sp != clamp_temp(s->heat_sp)) {
        s->heat_sp = clamp_temp(s->heat_sp);
        ok = false;
    }
    if (s->cool_sp != clamp_temp(s->cool_sp)) {
        s->cool_sp = clamp_temp(s->cool_sp);
        ok = false;
    }
    if ((unsigned)s->fan > (unsigned)AC_FAN_HIGH) {
        s->fan = AC_FAN_AUTO;
        ok = false;
    }
    if (s->fan_stash_valid && (unsigned)s->fan_stash > (unsigned)AC_FAN_HIGH) {
        s->fan_stash = AC_FAN_AUTO;
        s->fan_stash_valid = false;
        ok = false;
    }
    if (s->temp_stash_valid && s->temp_stash != clamp_temp(s->temp_stash)) {
        s->temp_stash = clamp_temp(s->temp_stash);
        s->temp_stash_valid = false;
        ok = false;
    }
    return ok;
}

uint8_t ac_active_temp(const ac_state_t *s)
{
    return s->mode == HsModeHeat ? s->heat_sp : s->cool_sp;
}

hs_fan_t ac_fan_to_hs(ac_fan_t fan)
{
    switch (fan) {
    case AC_FAN_LOW:
        return HsFanLow;
    case AC_FAN_MID_LOW:
        return HsFanMidLow;
    case AC_FAN_MID:
        return HsFanMid;
    case AC_FAN_MID_HIGH:
        return HsFanMidHigh;
    case AC_FAN_HIGH:
        return HsFanHigh;
    case AC_FAN_AUTO:
    default:
        return HsFanAuto;
    }
}

void ac_to_hs(const ac_state_t *s, hs_state_t *out)
{
    hs_state_default(out);
    out->power = s->power;
    out->mode = s->mode;
    out->temp = ac_active_temp(s);
    out->fan = ac_fan_to_hs(s->fan);
    out->sleep = s->sleep;
    out->turbo = s->turbo;
    out->economy = s->economy;
    out->clock_h = 0U;
    out->clock_m = 0U;
}

void ac_set_power_mode(ac_state_t *s, bool power, hs_mode_t mode)
{
    s->power = power;
    if (power && mode_valid(mode)) {
        s->mode = mode;
    }
}

void ac_set_power(ac_state_t *s, bool power)
{
    s->power = power;
}

void ac_set_setpoint(ac_state_t *s, bool heating, uint8_t temp)
{
    if (heating) {
        s->heat_sp = clamp_temp(temp);
        s->temp_stash_valid = false;
    } else {
        s->cool_sp = clamp_temp(temp);
    }
}

void ac_set_fan(ac_state_t *s, ac_fan_t fan)
{
    if ((unsigned)fan > (unsigned)AC_FAN_HIGH) {
        return;
    }
    s->fan = fan;
    s->fan_stash_valid = false;
}

static void apply_sleep_economy_change(ac_state_t *s, bool turned_on)
{
    if (turned_on) {
        if (!s->fan_stash_valid) {
            s->fan_stash = s->fan;
            s->fan_stash_valid = true;
        }
        s->fan = AC_FAN_LOW;
    } else if (!s->sleep && !s->economy && s->fan_stash_valid) {
        s->fan = s->fan_stash;
        s->fan_stash_valid = false;
    }
}

void ac_set_sleep(ac_state_t *s, bool on)
{
    if (s->sleep == on) {
        return;
    }
    s->sleep = on;
    apply_sleep_economy_change(s, on);
}

void ac_set_economy(ac_state_t *s, bool on)
{
    if (s->economy == on) {
        return;
    }
    s->economy = on;
    apply_sleep_economy_change(s, on);
}

void ac_set_turbo(ac_state_t *s, bool on)
{
    if (s->turbo == on) {
        return;
    }
    s->turbo = on;
    if (on) {
        if (s->mode == HsModeHeat) {
            if (!s->temp_stash_valid) {
                s->temp_stash = s->heat_sp;
                s->temp_stash_valid = true;
            }
            s->heat_sp = AC_TURBO_HEAT_TEMP;
        }
    } else if (s->temp_stash_valid) {
        s->heat_sp = s->temp_stash;
        s->temp_stash_valid = false;
    }
}

void ac_set_toggle(ac_state_t *s, ac_toggle_t which, bool on)
{
    switch (which) {
    case AC_TOGGLE_VSWING:
        s->vswing = on;
        break;
    case AC_TOGGLE_HSWING:
        s->hswing = on;
        break;
    case AC_TOGGLE_DISPLAY:
        s->display = on;
        break;
    default:
        break;
    }
}

static void copy_stash(ac_state_t *dst, const ac_state_t *src)
{
    dst->fan_stash_valid = src->fan_stash_valid;
    dst->fan_stash = src->fan_stash;
    dst->temp_stash_valid = src->temp_stash_valid;
    dst->temp_stash = src->temp_stash;
}

size_t ac_plan(const ac_state_t *sent, const ac_state_t *want, uint8_t cmds[AC_MAX_CMDS], ac_state_t *applied)
{
    size_t n = 0U;
    bool turbo_changed = sent->turbo != want->turbo;
    bool fan_preset_changed = sent->sleep != want->sleep || sent->economy != want->economy;

    if (!want->power) {
        *applied = *sent;
        copy_stash(applied, want);
        if (sent->power) {
            cmds[n++] = HsCmdPower;
            applied->power = false;
        }
        return n;
    }

    if (!sent->power) {
        cmds[n++] = HsCmdPower;
    }
    if (sent->mode != want->mode) {
        cmds[n++] = HsCmdMode;
    }
    /* A turbo frame in heat already carries temp 30 or the restored temp, as on the remote. */
    if (ac_active_temp(sent) != ac_active_temp(want) && !(turbo_changed && want->mode == HsModeHeat)) {
        cmds[n++] = HsCmdTemp;
    }
    /* A sleep or economy frame already carries the forced or restored fan, as on the remote. */
    if (sent->fan != want->fan && !fan_preset_changed) {
        cmds[n++] = HsCmdFan;
    }
    if (turbo_changed) {
        cmds[n++] = HsCmdTurbo;
    }
    if (sent->economy != want->economy) {
        cmds[n++] = HsCmdEconomy;
    }
    if (sent->sleep != want->sleep) {
        cmds[n++] = HsCmdSleep;
    }
    if (sent->vswing != want->vswing) {
        cmds[n++] = HsCmdVSwing;
    }
    if (sent->hswing != want->hswing) {
        cmds[n++] = HsCmdHSwing;
    }
    if (sent->display != want->display) {
        cmds[n++] = HsCmdDisplay;
    }
    *applied = *want;
    return n;
}

size_t ac_plan_resend(const ac_state_t *sent, const ac_state_t *want, ac_resend_t action,
                      uint8_t cmds[AC_MAX_CMDS], ac_state_t *applied)
{
    *applied = *sent;
    switch (action) {
    case AC_RESEND_STATE:
        if (!want->power) {
            *applied = *sent;
            copy_stash(applied, want);
            applied->power = false;
            cmds[0] = HsCmdPower;
            return 1U;
        }
        *applied = *want;
        applied->vswing = sent->vswing;
        applied->hswing = sent->hswing;
        applied->display = sent->display;
        cmds[0] = HsCmdTemp;
        return 1U;
    case AC_RESEND_VSWING_PULSE:
        cmds[0] = HsCmdVSwing;
        return 1U;
    case AC_RESEND_HSWING_PULSE:
        cmds[0] = HsCmdHSwing;
        return 1U;
    case AC_RESEND_DISPLAY_PULSE:
        cmds[0] = HsCmdDisplay;
        return 1U;
    case AC_RESEND_NONE:
    default:
        return 0U;
    }
}

uint8_t ac_sysmode_to_zcl(const ac_state_t *s)
{
    if (!s->power) {
        return AC_ZCL_SYSMODE_OFF;
    }
    switch (s->mode) {
    case HsModeCool:
        return AC_ZCL_SYSMODE_COOL;
    case HsModeDry:
        return AC_ZCL_SYSMODE_DRY;
    case HsModeFan:
        return AC_ZCL_SYSMODE_FAN_ONLY;
    case HsModeHeat:
    default:
        return AC_ZCL_SYSMODE_HEAT;
    }
}

bool ac_sysmode_from_zcl(uint8_t zcl, bool *power, hs_mode_t *mode)
{
    switch (zcl) {
    case AC_ZCL_SYSMODE_OFF:
        *power = false;
        return true;
    case AC_ZCL_SYSMODE_COOL:
        *power = true;
        *mode = HsModeCool;
        return true;
    case AC_ZCL_SYSMODE_HEAT:
        *power = true;
        *mode = HsModeHeat;
        return true;
    case AC_ZCL_SYSMODE_FAN_ONLY:
        *power = true;
        *mode = HsModeFan;
        return true;
    case AC_ZCL_SYSMODE_DRY:
        *power = true;
        *mode = HsModeDry;
        return true;
    default:
        return false;
    }
}

uint8_t ac_fan_to_zcl(ac_fan_t fan)
{
    switch (fan) {
    case AC_FAN_LOW:
    case AC_FAN_MID_LOW:
        return AC_ZCL_FANMODE_LOW;
    case AC_FAN_MID:
        return AC_ZCL_FANMODE_MEDIUM;
    case AC_FAN_MID_HIGH:
    case AC_FAN_HIGH:
        return AC_ZCL_FANMODE_HIGH;
    case AC_FAN_AUTO:
    default:
        return AC_ZCL_FANMODE_AUTO;
    }
}

bool ac_fan_from_zcl(uint8_t zcl, ac_fan_t *fan)
{
    switch (zcl) {
    case AC_ZCL_FANMODE_LOW:
        *fan = AC_FAN_LOW;
        return true;
    case AC_ZCL_FANMODE_MEDIUM:
        *fan = AC_FAN_MID;
        return true;
    case AC_ZCL_FANMODE_HIGH:
        *fan = AC_FAN_HIGH;
        return true;
    case AC_ZCL_FANMODE_AUTO:
        *fan = AC_FAN_AUTO;
        return true;
    default:
        return false;
    }
}

int16_t ac_temp_to_centi(uint8_t temp)
{
    return (int16_t)(temp * 100);
}

uint8_t ac_temp_from_centi(int16_t centi)
{
    int rounded = centi >= 0 ? (centi + 50) / 100 : (centi - 50) / 100;

    return clamp_temp(rounded);
}

bool ac_zcl_sysmode_uses_heating(uint8_t zcl_mode)
{
    return zcl_mode == AC_ZCL_SYSMODE_OFF || zcl_mode == AC_ZCL_SYSMODE_HEAT;
}

bool ac_bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_size)
{
    static const char digits[] = "0123456789ABCDEF";

    if (out == NULL || out_size == 0U) {
        return false;
    }
    if ((bytes == NULL && len > 0U) || out_size < 2U * len + 1U) {
        out[0] = '\0';
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        out[2U * i] = digits[bytes[i] >> 4];
        out[2U * i + 1U] = digits[bytes[i] & 0x0FU];
    }
    out[2U * len] = '\0';
    return true;
}
