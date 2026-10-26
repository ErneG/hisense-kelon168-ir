#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "double_reset.h"

static void test_first_reset_arms_detector(void)
{
    hac_double_reset_state_t state = {0};
    hac_double_reset_result_t result = hac_double_reset_arm_or_trigger(&state);

    assert(result == HAC_DOUBLE_RESET_ARMED);
    assert(hac_double_reset_is_armed(&state) == true);
}

static void test_second_reset_triggers_factory_reset_request(void)
{
    hac_double_reset_state_t state = {0};

    assert(hac_double_reset_arm_or_trigger(&state) == HAC_DOUBLE_RESET_ARMED);
    assert(hac_double_reset_arm_or_trigger(&state) == HAC_DOUBLE_RESET_TRIGGERED);
    assert(hac_double_reset_is_armed(&state) == false);
}

static void test_clear_disarms_detector(void)
{
    hac_double_reset_state_t state = {0};

    assert(hac_double_reset_arm_or_trigger(&state) == HAC_DOUBLE_RESET_ARMED);
    hac_double_reset_clear(&state);
    assert(hac_double_reset_is_armed(&state) == false);
}

static void test_usb_reset_does_not_qualify(void)
{
    assert(hac_double_reset_reason_qualifies(HAC_RST_USB) == false);
    assert(hac_double_reset_reason_qualifies(11) == false);
    assert(hac_double_reset_reason_qualifies(HAC_RST_JTAG) == false);
}

static void test_power_on_and_external_resets_qualify(void)
{
    assert(hac_double_reset_reason_qualifies(HAC_RST_POWERON) == true);
    assert(hac_double_reset_reason_qualifies(1) == true);
    assert(hac_double_reset_reason_qualifies(HAC_RST_EXT) == true);
    assert(hac_double_reset_reason_qualifies(HAC_RST_UNKNOWN) == true);
}

static void test_crash_resets_do_not_qualify(void)
{
    assert(hac_double_reset_reason_qualifies(HAC_RST_PANIC) == false);
    assert(hac_double_reset_reason_qualifies(HAC_RST_TASK_WDT) == false);
    assert(hac_double_reset_reason_qualifies(HAC_RST_BROWNOUT) == false);
    assert(hac_double_reset_reason_qualifies(HAC_RST_DEEPSLEEP) == false);
}

/* Mirrors commissioning.c: a non-qualifying reset clears the armed state, a qualifying one arms or triggers. */
static hac_double_reset_result_t boot(hac_double_reset_state_t *state, int reason)
{
    if (!hac_double_reset_reason_qualifies(reason)) {
        hac_double_reset_clear(state);
        return HAC_DOUBLE_RESET_NONE;
    }
    return hac_double_reset_arm_or_trigger(state);
}

static void test_repeated_usb_port_opens_never_trigger(void)
{
    hac_double_reset_state_t state = {0};

    for (int i = 0; i < 5; i++) {
        assert(boot(&state, HAC_RST_USB) == HAC_DOUBLE_RESET_NONE);
    }
    assert(hac_double_reset_is_armed(&state) == false);
}

static void test_usb_reset_between_power_cycles_cancels_window(void)
{
    hac_double_reset_state_t state = {0};

    assert(boot(&state, HAC_RST_POWERON) == HAC_DOUBLE_RESET_ARMED);
    assert(boot(&state, HAC_RST_USB) == HAC_DOUBLE_RESET_NONE);
    assert(boot(&state, HAC_RST_POWERON) == HAC_DOUBLE_RESET_ARMED);
}

static void test_two_power_on_resets_still_trigger(void)
{
    hac_double_reset_state_t state = {0};

    assert(boot(&state, HAC_RST_POWERON) == HAC_DOUBLE_RESET_ARMED);
    assert(boot(&state, HAC_RST_POWERON) == HAC_DOUBLE_RESET_TRIGGERED);
}

int main(void)
{
    test_usb_reset_does_not_qualify();
    test_power_on_and_external_resets_qualify();
    test_crash_resets_do_not_qualify();
    test_repeated_usb_port_opens_never_trigger();
    test_usb_reset_between_power_cycles_cancels_window();
    test_two_power_on_resets_still_trigger();
    test_first_reset_arms_detector();
    test_second_reset_triggers_factory_reset_request();
    test_clear_disarms_detector();
    return 0;
}
