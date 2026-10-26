#include "commissioning_policy.h"

bool hac_commissioning_should_start_network_steering(bool factory_new, bool joined)
{
    (void)factory_new;
    return !joined;
}

bool hac_commissioning_should_sample_factory_reset_gpio_at_boot(int gpio_num)
{
    if (gpio_num < 0) {
        return false;
    }

    /* GPIO8/GPIO9 are ESP32-C6 strapping pins and should not be used for boot-time service-button sampling. */
    return gpio_num != 8 && gpio_num != 9;
}
