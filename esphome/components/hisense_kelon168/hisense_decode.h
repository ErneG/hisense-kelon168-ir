#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {
#include "hisense_codec.h"
}

namespace hisense_kelon168 {

struct DecodedFrame {
  hs_state_t state;
  uint8_t cmd;
  bool auto_mode;
};

static inline bool within(int32_t v, int32_t lo, int32_t hi) { return v >= lo && v <= hi; }

static inline bool decode_timings(const int32_t *t, size_t n, uint8_t out[HS_FRAME_BYTES]) {
  static const uint8_t sizes[3] = {6, 8, 7};
  size_t i = 0;
  while (i + 1 < n && i < 4 && !(t[i] > 0 && within(t[i], 6500, 11500) && within(abs(t[i + 1]), 3000, 6200)))
    i++;
  if (i + 1 >= n || i >= 4)
    return false;
  i += 2;
  size_t pos = 0;
  for (size_t si = 0; si < 3; si++) {
    for (size_t bi = 0; bi < sizes[si]; bi++) {
      uint8_t b = 0;
      for (uint8_t bit = 0; bit < 8; bit++) {
        if (i + 1 >= n)
          return false;
        if (!(t[i] > 0 && within(t[i], 250, 1000)))
          return false;
        int32_t sp = abs(t[i + 1]);
        if (!within(sp, 250, 2400))
          return false;
        if (sp > 1100)
          b |= (uint8_t) (1u << bit);
        i += 2;
      }
      out[pos + bi] = b;
    }
    pos += sizes[si];
    if (i >= n || !(t[i] > 0 && within(t[i], 250, 1000)))
      return false;
    i++;
    if (si < 2) {
      if (i >= n || abs(t[i]) < 5000)
        return false;
      i++;
    }
  }
  return true;
}

static inline bool frame_checksums_ok(const uint8_t f[HS_FRAME_BYTES]) {
  if (f[0] != 0x83 || f[1] != 0x06)
    return false;
  uint8_t c1 = 0, c2 = 0;
  for (size_t i = 2; i < 13; i++)
    c1 ^= f[i];
  for (size_t i = 14; i < 20; i++)
    c2 ^= f[i];
  return c1 == f[13] && c2 == f[20];
}

static inline bool parse_frame(const uint8_t f[HS_FRAME_BYTES], DecodedFrame *d) {
  if (!frame_checksums_ok(f))
    return false;
  hs_state_t s;
  hs_state_default(&s);
  d->auto_mode = false;
  uint8_t mb = f[3] & 7;
  switch (mb) {
    case 0: s.mode = HsModeHeat; break;
    case 2: s.mode = HsModeCool; break;
    case 3: s.mode = HsModeDry; break;
    case 4: s.mode = HsModeFan; break;
    case 1: d->auto_mode = true; break;
    default: return false;
  }
  s.power = (f[18] & 0x10) != 0;
  s.temp = (uint8_t) (16 + (f[3] >> 4));
  bool half = (f[17] & 0x40) != 0;
  switch (f[2] & 3) {
    case 0: s.fan = HsFanAuto; break;
    case 1: s.fan = half ? HsFanMidHigh : HsFanHigh; break;
    case 2: s.fan = HsFanMid; break;
    default: s.fan = half ? HsFanMidLow : HsFanLow; break;
  }
  s.sleep = (f[2] & 0x08) != 0;
  s.turbo = f[5] == 0x90;
  s.economy = (f[14] & 0x20) != 0;
  s.clock_h = f[6] & 0x1F;
  s.clock_m = f[7] & 0x3F;
  d->state = s;
  d->cmd = f[15];
  return true;
}

}  // namespace hisense_kelon168
