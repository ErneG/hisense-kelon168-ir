#include <assert.h>
#include <stdbool.h>

#include "commissioning_policy.h"

static void test_factory_new_device_should_start_network_steering(void)
{
    assert(hac_commissioning_should_start_network_steering(true, false) == true);
}

static void test_non_factory_new_device_that_is_not_joined_should_retry_network_steering(void)
{
    assert(hac_commissioning_should_start_network_steering(false, false) == true);
}

static void test_joined_device_should_not_restart_network_steering(void)
{
    assert(hac_commissioning_should_start_network_steering(false, true) == false);
}

static void test_bootstrap_pins_are_not_sampled_for_boot_time_factory_reset(void)
{
    assert(hac_commissioning_should_sample_factory_reset_gpio_at_boot(8) == false);
    assert(hac_commissioning_should_sample_factory_reset_gpio_at_boot(9) == false);
}

static void test_regular_gpio_can_be_sampled_for_boot_time_factory_reset(void)
{
    assert(hac_commissioning_should_sample_factory_reset_gpio_at_boot(1) == true);
    assert(hac_commissioning_should_sample_factory_reset_gpio_at_boot(-1) == false);
}

int main(void)
{
    test_factory_new_device_should_start_network_steering();
    test_non_factory_new_device_that_is_not_joined_should_retry_network_steering();
    test_joined_device_should_not_restart_network_steering();
    test_bootstrap_pins_are_not_sampled_for_boot_time_factory_reset();
    test_regular_gpio_can_be_sampled_for_boot_time_factory_reset();
    return 0;
}
