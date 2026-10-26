#include <cstdio>
#include <vector>

#include "hisense_decode.h"

using namespace hisense_kelon168;

static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

static std::vector<int32_t> signed_timings(const uint8_t f[HS_FRAME_BYTES]) {
  uint32_t t[HS_TIMINGS_COUNT];
  size_t n = hs_frame_to_timings(f, t, HS_TIMINGS_COUNT);
  std::vector<int32_t> v;
  for (size_t i = 0; i < n; i++) v.push_back(i % 2 == 0 ? (int32_t) t[i] : -(int32_t) t[i]);
  return v;
}

int main() {
  int count = 0;
  const hs_mode_t modes[] = {HsModeHeat, HsModeCool, HsModeDry, HsModeFan};
  const uint8_t cmds[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x08, 0x0C, 0x11};
  for (hs_mode_t m : modes)
    for (int fan = 0; fan <= HsFanLow; fan++)
      for (uint8_t temp = 16; temp <= 30; temp += 7)
        for (int flags = 0; flags < 8; flags++)
          for (int pw = 0; pw < 2; pw++)
            for (uint8_t cmd : cmds) {
              hs_state_t s;
              hs_state_default(&s);
              s.mode = m; s.fan = (hs_fan_t) fan; s.temp = temp; s.power = pw;
              s.sleep = flags & 1; s.turbo = flags & 2; s.economy = flags & 4;
              s.clock_h = 13; s.clock_m = 37;
              uint8_t f[HS_FRAME_BYTES], g[HS_FRAME_BYTES];
              CHECK(hs_build_frame(&s, cmd, f));
              auto tm = signed_timings(f);
              CHECK(decode_timings(tm.data(), tm.size(), g));
              for (int i = 0; i < HS_FRAME_BYTES; i++) CHECK(f[i] == g[i]);
              DecodedFrame d;
              CHECK(parse_frame(g, &d));
              CHECK(d.cmd == cmd);
              CHECK(d.state.power == s.power && d.state.mode == s.mode && d.state.temp == s.temp);
              CHECK(d.state.fan == s.fan && d.state.sleep == s.sleep);
              CHECK(d.state.turbo == s.turbo && d.state.economy == s.economy);
              CHECK(d.state.clock_h == 13 && d.state.clock_m == 37);
              count++;
            }
  hs_state_t s;
  hs_state_default(&s);
  uint8_t f[HS_FRAME_BYTES], g[HS_FRAME_BYTES];
  hs_build_frame(&s, 0x02, f);
  auto tm = signed_timings(f);
  for (auto &x : tm) x = x * 115 / 100;
  CHECK(decode_timings(tm.data(), tm.size(), g));
  f[13] ^= 1;
  CHECK(!frame_checksums_ok(f));
  f[13] ^= 1;
  f[20] ^= 1;
  DecodedFrame d;
  CHECK(!parse_frame(f, &d));
  printf("%d frames round-tripped, %d failures\n", count, fails);
  return fails ? 1 : 0;
}
