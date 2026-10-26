#include "double_reset.h"

#include <stddef.h>

#define HAC_DOUBLE_RESET_MAGIC   0x44524431UL
#define HAC_DOUBLE_RESET_VERSION 1U

static bool hac_double_reset_state_is_valid(const hac_double_reset_state_t *state)
{
    return state != NULL &&
           state->magic == HAC_DOUBLE_RESET_MAGIC &&
           state->version == HAC_DOUBLE_RESET_VERSION;
}

bool hac_double_reset_reason_qualifies(int reason)
{
    switch (reason) {
    case HAC_RST_DEEPSLEEP:
    case HAC_RST_PANIC:
    case HAC_RST_INT_WDT:
    case HAC_RST_TASK_WDT:
    case HAC_RST_WDT:
    case HAC_RST_BROWNOUT:
    case HAC_RST_CPU_LOCKUP:
    case HAC_RST_USB:
    case HAC_RST_JTAG:
        return false;
    default:
        return true;
    }
}

hac_double_reset_result_t hac_double_reset_arm_or_trigger(hac_double_reset_state_t *state)
{
    if (state == NULL) {
        return HAC_DOUBLE_RESET_NONE;
    }

    if (hac_double_reset_state_is_valid(state) && state->armed != 0U) {
        hac_double_reset_clear(state);
        return HAC_DOUBLE_RESET_TRIGGERED;
    }

    state->magic = HAC_DOUBLE_RESET_MAGIC;
    state->version = HAC_DOUBLE_RESET_VERSION;
    state->armed = 1U;
    state->reserved[0] = 0U;
    state->reserved[1] = 0U;
    return HAC_DOUBLE_RESET_ARMED;
}

void hac_double_reset_clear(hac_double_reset_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->magic = 0U;
    state->version = 0U;
    state->armed = 0U;
    state->reserved[0] = 0U;
    state->reserved[1] = 0U;
}

bool hac_double_reset_is_armed(const hac_double_reset_state_t *state)
{
    return hac_double_reset_state_is_valid(state) && state->armed != 0U;
}
