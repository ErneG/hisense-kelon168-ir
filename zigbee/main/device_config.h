#pragma once

#include <stdint.h>

typedef struct {
    uint32_t primary_channel_mask;
    uint8_t min_join_lqi;
} hac_device_config_t;
