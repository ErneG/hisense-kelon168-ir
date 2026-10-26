#include "hisense_codec.h"

#define HDR_MARK 9000U
#define HDR_SPACE 4600U
#define BIT_MARK 560U
#define ONE_SPACE 1680U
#define ZERO_SPACE 600U
#define FOOTER_SPACE 8000U

static const uint8_t section_sizes[3] = {6, 8, 7};

void hs_state_default(hs_state_t* s) {
    s->power = true;
    s->mode = HsModeHeat;
    s->temp = 22;
    s->fan = HsFanAuto;
    s->sleep = false;
    s->turbo = false;
    s->economy = false;
    s->clock_h = 0;
    s->clock_m = 0;
}

bool hs_state_valid(const hs_state_t* s) {
    if(s->mode != HsModeHeat && s->mode != HsModeCool && s->mode != HsModeDry &&
       s->mode != HsModeFan) {
        return false;
    }
    if((unsigned)s->fan > (unsigned)HsFanLow) return false;
    if(s->temp < 16 || s->temp > 30) return false;
    if(s->clock_h > 23 || s->clock_m > 59) return false;
    return true;
}

bool hs_build_frame(const hs_state_t* s, uint8_t cmd, uint8_t out[HS_FRAME_BYTES]) {
    if(!hs_state_valid(s)) return false;

    uint8_t fan_bits = 0;
    uint8_t half = 0;
    switch(s->fan) {
    case HsFanAuto:
        fan_bits = 0;
        break;
    case HsFanHigh:
        fan_bits = 1;
        break;
    case HsFanMidHigh:
        fan_bits = 1;
        half = 1;
        break;
    case HsFanMid:
        fan_bits = 2;
        break;
    case HsFanLow:
        fan_bits = 3;
        break;
    case HsFanMidLow:
        fan_bits = 3;
        half = 1;
        break;
    }

    for(size_t i = 0; i < HS_FRAME_BYTES; i++) out[i] = 0;
    out[0] = 0x83;
    out[1] = 0x06;
    out[2] = fan_bits | (cmd == 0x01 ? 0x04 : 0) | (s->sleep ? 0x08 : 0) |
             (cmd == 0x07 ? 0x80 : 0);
    out[3] = (uint8_t)((uint8_t)s->mode | ((s->temp - 16) << 4));
    out[5] = s->turbo ? 0x90 : 0;
    out[6] = (uint8_t)(0x80 | s->clock_h | (cmd == 0x00 ? 0x20 : 0));
    out[7] = s->clock_m;
    out[8] = (cmd == 0x07 ? 0x40 : 0) | (cmd == 0x08 ? 0x80 : 0);
    out[14] = s->economy ? 0x20 : 0;
    out[15] = cmd;
    out[17] = half ? 0x40 : 0;
    out[18] = s->power ? 0x38 : 0x28;

    uint8_t c1 = 0;
    for(size_t i = 2; i < 13; i++) c1 ^= out[i];
    uint8_t c2 = 0;
    for(size_t i = 14; i < 20; i++) c2 ^= out[i];
    out[13] = c1;
    out[20] = c2;
    return true;
}

size_t hs_frame_to_timings(const uint8_t frame[HS_FRAME_BYTES], uint32_t* out, size_t max) {
    if(max < HS_TIMINGS_COUNT) return 0;
    size_t n = 0;
    out[n++] = HDR_MARK;
    out[n++] = HDR_SPACE;
    size_t pos = 0;
    for(size_t si = 0; si < 3; si++) {
        for(size_t bi = 0; bi < section_sizes[si]; bi++) {
            uint8_t b = frame[pos + bi];
            for(uint8_t i = 0; i < 8; i++) {
                out[n++] = BIT_MARK;
                out[n++] = ((b >> i) & 1) ? ONE_SPACE : ZERO_SPACE;
            }
        }
        pos += section_sizes[si];
        out[n++] = BIT_MARK;
        if(si < 2) out[n++] = FOOTER_SPACE;
    }
    return n;
}
