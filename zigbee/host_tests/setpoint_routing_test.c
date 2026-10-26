#include <stdio.h>

#include "ac_logic.h"

static int s_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_mode_routing(void)
{
    CHECK(ac_zcl_sysmode_uses_heating(AC_ZCL_SYSMODE_OFF));
    CHECK(ac_zcl_sysmode_uses_heating(AC_ZCL_SYSMODE_HEAT));
    CHECK(!ac_zcl_sysmode_uses_heating(AC_ZCL_SYSMODE_COOL));
    CHECK(!ac_zcl_sysmode_uses_heating(AC_ZCL_SYSMODE_DRY));
    CHECK(!ac_zcl_sysmode_uses_heating(AC_ZCL_SYSMODE_FAN_ONLY));
}

static void test_all_pairs_independent(void)
{
    for (unsigned heat = AC_TEMP_MIN; heat <= AC_TEMP_MAX; heat++) {
        for (unsigned cool = AC_TEMP_MIN; cool <= AC_TEMP_MAX; cool++) {
            ac_state_t s;

            ac_state_default(&s);
            ac_set_setpoint(&s, true, (uint8_t)heat);
            ac_set_setpoint(&s, false, (uint8_t)cool);
            CHECK(s.heat_sp == heat);
            CHECK(s.cool_sp == cool);
            ac_set_setpoint(&s, false, (uint8_t)AC_TEMP_MIN);
            CHECK(s.heat_sp == heat);
            CHECK(s.cool_sp == AC_TEMP_MIN);
        }
    }
}

static void test_centi_and_active(void)
{
    ac_state_t s;
    uint8_t zcl_modes[] = {AC_ZCL_SYSMODE_HEAT, AC_ZCL_SYSMODE_COOL};

    CHECK(ac_temp_from_centi(1700) == 17U);
    CHECK(ac_temp_from_centi(1900) == 19U);
    CHECK(ac_temp_from_centi(1600) == 16U);
    CHECK(ac_temp_from_centi(3000) == 30U);

    ac_state_default(&s);
    ac_set_setpoint(&s, true, 22U);
    ac_set_setpoint(&s, false, 17U);
    for (unsigned i = 0; i < sizeof(zcl_modes); i++) {
        bool power;
        hs_mode_t mode;

        CHECK(ac_sysmode_from_zcl(zcl_modes[i], &power, &mode));
        ac_set_power_mode(&s, power, mode);
        CHECK(ac_active_temp(&s) == (ac_zcl_sysmode_uses_heating(zcl_modes[i]) ? 22U : 17U));
    }
}

/* Every heat/cool pair in 16-30 C must be accepted in both write orders, survive the centi round trip used by the
 * 0xFC20 int16 attributes, and drive the unit temp from the setpoint of the active mode. */
static void test_all_pairs_both_orders_and_active_temp(void)
{
    for (unsigned t = AC_TEMP_MIN; t <= AC_TEMP_MAX; t++) {
        CHECK(ac_temp_from_centi(ac_temp_to_centi((uint8_t)t)) == t);
    }
    for (unsigned heat = AC_TEMP_MIN; heat <= AC_TEMP_MAX; heat++) {
        for (unsigned cool = AC_TEMP_MIN; cool <= AC_TEMP_MAX; cool++) {
            ac_state_t s;
            ac_state_t sent;
            ac_state_t applied;
            uint8_t cmds[AC_MAX_CMDS];

            ac_state_default(&s);
            ac_set_setpoint(&s, false, ac_temp_from_centi(ac_temp_to_centi((uint8_t)cool)));
            ac_set_setpoint(&s, true, ac_temp_from_centi(ac_temp_to_centi((uint8_t)heat)));
            CHECK(s.heat_sp == heat && s.cool_sp == cool);

            ac_set_power_mode(&s, true, HsModeCool);
            CHECK(ac_active_temp(&s) == cool);
            ac_set_power_mode(&s, true, HsModeHeat);
            CHECK(ac_active_temp(&s) == heat);

            sent = s;
            (void)ac_plan(&sent, &s, cmds, &applied);
            CHECK(applied.heat_sp == heat && applied.cool_sp == cool);
        }
    }
}

int main(void)
{
    test_all_pairs_both_orders_and_active_temp();
    test_mode_routing();
    test_all_pairs_independent();
    test_centi_and_active();
    printf("setpoint_routing: %s (%d failure(s))\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
