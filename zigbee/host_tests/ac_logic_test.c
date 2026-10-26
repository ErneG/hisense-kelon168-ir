#include <stdio.h>
#include <string.h>

#include "ac_logic.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static int cmds_equal(const uint8_t *got, size_t n, const uint8_t *exp, size_t en)
{
    if (n != en) {
        printf("  got %u cmds:", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            printf(" %02x", got[i]);
        }
        printf("\n");
        return 0;
    }
    return memcmp(got, exp, n) == 0;
}

#define EXPECT_PLAN(sent, want, ...)                                                          \
    do {                                                                                      \
        static const uint8_t exp_[] = {0, __VA_ARGS__};                                          \
        uint8_t got_[AC_MAX_CMDS];                                                            \
        ac_state_t applied_;                                                                  \
        size_t n_ = ac_plan(&(sent), &(want), got_, &applied_);                               \
        CHECK(cmds_equal(got_, n_, exp_ + 1, sizeof(exp_) - 1U));                                      \
        (sent) = applied_;                                                                    \
    } while (0)

static size_t plan(ac_state_t *sent, const ac_state_t *want, uint8_t *cmds)
{
    ac_state_t applied;
    size_t n = ac_plan(sent, want, cmds, &applied);

    *sent = applied;
    return n;
}

static void test_no_change_sends_nothing(void)
{
    ac_state_t sent, want;
    uint8_t cmds[AC_MAX_CMDS];

    ac_state_default(&sent);
    want = sent;
    CHECK(plan(&sent, &want, cmds) == 0U);
    want.power = true;
    sent.power = true;
    CHECK(plan(&sent, &want, cmds) == 0U);
}

static void test_off_to_mode_sends_power_first(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    want = sent;
    ac_set_power_mode(&want, true, HsModeHeat);
    EXPECT_PLAN(sent, want, HsCmdPower);
    CHECK(sent.power);

    ac_set_power_mode(&want, false, HsModeHeat);
    EXPECT_PLAN(sent, want, HsCmdPower);
    ac_set_power_mode(&want, true, HsModeCool);
    EXPECT_PLAN(sent, want, HsCmdPower, HsCmdMode, HsCmdTemp);
}

static void test_full_order(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    want = sent;
    want.power = true;
    want.mode = HsModeCool;
    want.cool_sp = 19;
    want.fan = AC_FAN_MID_HIGH;
    want.turbo = true;
    want.vswing = true;
    want.hswing = true;
    want.display = true;
    EXPECT_PLAN(sent, want, HsCmdPower, HsCmdMode, HsCmdTemp, HsCmdFan, HsCmdTurbo, HsCmdVSwing, HsCmdHSwing,
                HsCmdDisplay);
    CHECK(memcmp(&sent, &want, sizeof(want)) == 0);

    want.economy = true;
    want.sleep = true;
    want.fan = AC_FAN_LOW;
    EXPECT_PLAN(sent, want, HsCmdEconomy, HsCmdSleep);
}

static void test_power_off_defers_other_changes(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    sent.power = true;
    want = sent;
    want.power = false;
    want.cool_sp = 18;
    want.vswing = true;
    EXPECT_PLAN(sent, want, HsCmdPower);
    CHECK(!sent.power);
    CHECK(!sent.vswing);

    EXPECT_PLAN(sent, want);

    want.power = true;
    want.mode = HsModeCool;
    EXPECT_PLAN(sent, want, HsCmdPower, HsCmdMode, HsCmdTemp, HsCmdVSwing);
}

static void test_mode_change_temp_only_when_active_setpoint_differs(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    sent.power = true;
    sent.heat_sp = 24;
    sent.cool_sp = 24;
    want = sent;
    want.mode = HsModeCool;
    EXPECT_PLAN(sent, want, HsCmdMode);

    want.heat_sp = 20;
    EXPECT_PLAN(sent, want);
    want.mode = HsModeHeat;
    EXPECT_PLAN(sent, want, HsCmdMode, HsCmdTemp);
}

static void test_sleep_economy_force_fan_low_and_restore(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    sent.power = true;
    sent.fan = AC_FAN_HIGH;
    want = sent;

    ac_set_sleep(&want, true);
    CHECK(want.fan == AC_FAN_LOW);
    CHECK(want.fan_stash_valid && want.fan_stash == AC_FAN_HIGH);
    EXPECT_PLAN(sent, want, HsCmdSleep);

    ac_set_economy(&want, true);
    CHECK(want.fan == AC_FAN_LOW);
    CHECK(want.fan_stash == AC_FAN_HIGH);
    EXPECT_PLAN(sent, want, HsCmdEconomy);

    ac_set_sleep(&want, false);
    CHECK(want.fan == AC_FAN_LOW);
    EXPECT_PLAN(sent, want, HsCmdSleep);

    ac_set_economy(&want, false);
    CHECK(want.fan == AC_FAN_HIGH);
    CHECK(!want.fan_stash_valid);
    EXPECT_PLAN(sent, want, HsCmdEconomy);

    ac_set_sleep(&want, true);
    ac_set_fan(&want, AC_FAN_MID);
    CHECK(!want.fan_stash_valid);
    ac_set_sleep(&want, false);
    CHECK(want.fan == AC_FAN_MID);
}

static void test_turbo_heat_sets_30_and_restores(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    sent.power = true;
    sent.heat_sp = 21;
    want = sent;

    ac_set_turbo(&want, true);
    CHECK(want.heat_sp == 30);
    CHECK(want.temp_stash_valid && want.temp_stash == 21);
    EXPECT_PLAN(sent, want, HsCmdTurbo);

    ac_set_turbo(&want, false);
    CHECK(want.heat_sp == 21);
    CHECK(!want.temp_stash_valid);
    EXPECT_PLAN(sent, want, HsCmdTurbo);

    ac_set_turbo(&want, true);
    ac_set_setpoint(&want, true, 25);
    CHECK(!want.temp_stash_valid);
    ac_set_turbo(&want, false);
    CHECK(want.heat_sp == 25);

    want.mode = HsModeCool;
    sent = want;
    ac_set_turbo(&want, true);
    CHECK(want.cool_sp == sent.cool_sp);
    CHECK(!want.temp_stash_valid);
    EXPECT_PLAN(sent, want, HsCmdTurbo);
}

static void test_toggles_send_only_on_change(void)
{
    ac_state_t sent, want;

    ac_state_default(&sent);
    sent.power = true;
    want = sent;

    ac_set_toggle(&want, AC_TOGGLE_VSWING, true);
    EXPECT_PLAN(sent, want, HsCmdVSwing);
    ac_set_toggle(&want, AC_TOGGLE_VSWING, true);
    EXPECT_PLAN(sent, want);
    ac_set_toggle(&want, AC_TOGGLE_VSWING, false);
    EXPECT_PLAN(sent, want, HsCmdVSwing);

    ac_set_toggle(&want, AC_TOGGLE_HSWING, true);
    ac_set_toggle(&want, AC_TOGGLE_DISPLAY, true);
    EXPECT_PLAN(sent, want, HsCmdHSwing, HsCmdDisplay);
    ac_set_toggle(&want, AC_TOGGLE_HSWING, false);
    ac_set_toggle(&want, AC_TOGGLE_HSWING, true);
    EXPECT_PLAN(sent, want);
}

static void test_resend(void)
{
    ac_state_t sent, want, applied;
    uint8_t cmds[AC_MAX_CMDS];

    ac_state_default(&sent);
    sent.power = true;
    want = sent;
    want.vswing = true;

    CHECK(ac_plan_resend(&sent, &want, AC_RESEND_STATE, cmds, &applied) == 1U && cmds[0] == HsCmdTemp);
    CHECK(!applied.vswing);

    CHECK(ac_plan_resend(&sent, &want, AC_RESEND_VSWING_PULSE, cmds, &applied) == 1U && cmds[0] == HsCmdVSwing);
    CHECK(memcmp(&applied, &sent, sizeof(sent)) == 0);
    CHECK(ac_plan_resend(&sent, &want, AC_RESEND_HSWING_PULSE, cmds, &applied) == 1U && cmds[0] == HsCmdHSwing);
    CHECK(ac_plan_resend(&sent, &want, AC_RESEND_DISPLAY_PULSE, cmds, &applied) == 1U && cmds[0] == HsCmdDisplay);
    CHECK(ac_plan_resend(&sent, &want, AC_RESEND_NONE, cmds, &applied) == 0U);

    want.power = false;
    CHECK(ac_plan_resend(&sent, &want, AC_RESEND_STATE, cmds, &applied) == 1U && cmds[0] == HsCmdPower);
    CHECK(!applied.power);
}

static void test_frames_from_state(void)
{
    ac_state_t want;
    hs_state_t hs;
    uint8_t frame[HS_FRAME_BYTES];
    static const uint8_t all_cmds[] = {HsCmdPower, HsCmdMode,    HsCmdTemp,  HsCmdFan,    HsCmdTurbo,
                                       HsCmdEconomy, HsCmdSleep, HsCmdVSwing, HsCmdHSwing, HsCmdDisplay};

    ac_state_default(&want);
    want.power = true;
    want.mode = HsModeCool;
    want.cool_sp = 18;
    want.fan = AC_FAN_MID_LOW;
    ac_to_hs(&want, &hs);
    CHECK(hs.temp == 18 && hs.fan == HsFanMidLow && hs.clock_h == 0 && hs.clock_m == 0);
    for (size_t i = 0; i < sizeof(all_cmds); i++) {
        CHECK(hs_build_frame(&hs, all_cmds[i], frame));
        CHECK(frame[15] == all_cmds[i]);
        CHECK(frame[6] == (all_cmds[i] == HsCmdDisplay ? 0xA0 : 0x80));
        CHECK(frame[7] == 0x00);
        CHECK(frame[18] == 0x38);
    }
    CHECK(frame[3] == (uint8_t)(HsModeCool | ((18 - 16) << 4)));
    CHECK((frame[2] & 0x03) == 3 && frame[17] == 0x40);

    want.power = false;
    ac_to_hs(&want, &hs);
    CHECK(hs_build_frame(&hs, HsCmdPower, frame) && frame[18] == 0x28);

    CHECK(ac_fan_to_hs(AC_FAN_AUTO) == HsFanAuto);
    CHECK(ac_fan_to_hs(AC_FAN_LOW) == HsFanLow);
    CHECK(ac_fan_to_hs(AC_FAN_MID) == HsFanMid);
    CHECK(ac_fan_to_hs(AC_FAN_MID_HIGH) == HsFanMidHigh);
    CHECK(ac_fan_to_hs(AC_FAN_HIGH) == HsFanHigh);
}

static void test_zcl_mapping(void)
{
    ac_state_t s;
    bool power = true;
    hs_mode_t mode = HsModeHeat;
    ac_fan_t fan;

    ac_state_default(&s);
    CHECK(ac_sysmode_to_zcl(&s) == AC_ZCL_SYSMODE_OFF);
    s.power = true;
    s.mode = HsModeDry;
    CHECK(ac_sysmode_to_zcl(&s) == AC_ZCL_SYSMODE_DRY);
    s.mode = HsModeFan;
    CHECK(ac_sysmode_to_zcl(&s) == AC_ZCL_SYSMODE_FAN_ONLY);
    CHECK(ac_sysmode_from_zcl(AC_ZCL_SYSMODE_COOL, &power, &mode) && power && mode == HsModeCool);
    CHECK(ac_sysmode_from_zcl(AC_ZCL_SYSMODE_OFF, &power, &mode) && !power && mode == HsModeCool);
    CHECK(!ac_sysmode_from_zcl(0x01, &power, &mode));

    CHECK(ac_fan_to_zcl(AC_FAN_MID_LOW) == AC_ZCL_FANMODE_LOW);
    CHECK(ac_fan_to_zcl(AC_FAN_MID_HIGH) == AC_ZCL_FANMODE_HIGH);
    CHECK(ac_fan_from_zcl(AC_ZCL_FANMODE_MEDIUM, &fan) && fan == AC_FAN_MID);
    CHECK(!ac_fan_from_zcl(0x00, &fan));

    CHECK(ac_temp_from_centi(2150) == 22);
    CHECK(ac_temp_from_centi(2149) == 21);
    CHECK(ac_temp_from_centi(1000) == 16);
    CHECK(ac_temp_from_centi(3500) == 30);
    CHECK(ac_temp_from_centi(-500) == 16);
    CHECK(ac_temp_to_centi(23) == 2300);
}

static void test_sanitize(void)
{
    ac_state_t s;

    ac_state_default(&s);
    CHECK(ac_state_sanitize(&s));
    s.mode = (hs_mode_t)1;
    s.heat_sp = 40;
    s.fan = (ac_fan_t)9;
    CHECK(!ac_state_sanitize(&s));
    CHECK(s.mode == HsModeHeat && s.heat_sp == 30 && s.fan == AC_FAN_AUTO);
}

static void test_bytes_to_hex(void)
{
    const uint8_t frame[HS_FRAME_BYTES] = {0x83, 0x06, 0x04, 0x60, 0x00, 0x00, 0x8A, 0x2C, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAB, 0x39};
    char hex[AC_FRAME_HEX_SIZE];
    char small[4];

    CHECK(ac_bytes_to_hex(frame, HS_FRAME_BYTES, hex, sizeof(hex)));
    CHECK(strlen(hex) == 42U);
    CHECK(strcmp(hex, "8306046000008A2C0000000000000000000000AB39") == 0);
    CHECK(!ac_bytes_to_hex(frame, 2U, small, sizeof(small)));
    CHECK(small[0] == '\0');
}

int main(void)
{
    test_bytes_to_hex();
    test_no_change_sends_nothing();
    test_off_to_mode_sends_power_first();
    test_full_order();
    test_power_off_defers_other_changes();
    test_mode_change_temp_only_when_active_setpoint_differs();
    test_sleep_economy_force_fan_low_and_restore();
    test_turbo_heat_sets_30_and_restores();
    test_toggles_send_only_on_change();
    test_resend();
    test_frames_from_state();
    test_zcl_mapping();
    test_sanitize();
    printf("ac_logic: %s (%d failure(s))\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
