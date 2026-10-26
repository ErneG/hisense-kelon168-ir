#pragma once

#include <stdbool.h>

bool hac_commissioning_should_start_network_steering(bool factory_new, bool joined);
bool hac_commissioning_should_sample_factory_reset_gpio_at_boot(int gpio_num);
