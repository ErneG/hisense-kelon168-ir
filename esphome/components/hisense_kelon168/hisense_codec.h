#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define HS_FRAME_BYTES 21
#define HS_TIMINGS_COUNT 343
#define HS_CARRIER_HZ 38000
#define HS_DUTY_PERCENT 50

typedef enum {
    HsModeHeat = 0,
    HsModeCool = 2,
    HsModeDry = 3,
    HsModeFan = 4,
} hs_mode_t;

typedef enum {
    HsFanAuto = 0,
    HsFanHigh,
    HsFanMidHigh,
    HsFanMid,
    HsFanMidLow,
    HsFanLow,
} hs_fan_t;

typedef enum {
    HsCmdDisplay = 0x00,
    HsCmdPower = 0x01,
    HsCmdTemp = 0x02,
    HsCmdSleep = 0x03,
    HsCmdTurbo = 0x04,
    HsCmdMode = 0x06,
    HsCmdVSwing = 0x07,
    HsCmdHSwing = 0x08,
    HsCmdEconomy = 0x0C,
    HsCmdFan = 0x11,
} hs_cmd_t;

typedef struct {
    bool power;
    hs_mode_t mode;
    uint8_t temp;
    hs_fan_t fan;
    bool sleep;
    bool turbo;
    bool economy;
    uint8_t clock_h;
    uint8_t clock_m;
} hs_state_t;

void hs_state_default(hs_state_t* s);
bool hs_state_valid(const hs_state_t* s);
bool hs_build_frame(const hs_state_t* s, uint8_t cmd, uint8_t out[HS_FRAME_BYTES]);
size_t hs_frame_to_timings(const uint8_t frame[HS_FRAME_BYTES], uint32_t* out, size_t max);
