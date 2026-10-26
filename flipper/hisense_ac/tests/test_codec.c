#include <stdio.h>
#include <string.h>
#include "../hisense_codec.h"
#include "test_vectors.h"

_Static_assert(HS_CARRIER_HZ == HS_PY_CARRIER, "carrier");
_Static_assert(HS_DUTY_PERCENT == HS_PY_DUTY, "duty");
_Static_assert(HS_TIMING_LEN == HS_TIMINGS_COUNT, "timing count");

int main(void) {
    int pass = 0;
    for(int i = 0; i < HS_VECTOR_COUNT; i++) {
        uint8_t out[HS_FRAME_BYTES];
        if(hs_build_frame(&hs_vectors[i].state, hs_vectors[i].cmd, out) &&
           memcmp(out, hs_vectors[i].frame, HS_FRAME_BYTES) == 0) {
            pass++;
        } else {
            printf("FAIL frame %s\n", hs_vectors[i].name);
        }
    }
    printf("frames: %d/%d\n", pass, HS_VECTOR_COUNT);

    int tpass = 0;
    for(int i = 0; i < HS_TIMING_VECTOR_COUNT; i++) {
        uint32_t t[HS_TIMINGS_COUNT];
        const uint8_t* fr = hs_vectors[hs_timing_vector_index[i]].frame;
        size_t n = hs_frame_to_timings(fr, t, HS_TIMINGS_COUNT);
        if(n == HS_TIMING_LEN && memcmp(t, hs_timing_vectors[i], sizeof(t)) == 0) {
            tpass++;
        } else {
            printf("FAIL timings %d\n", i);
        }
    }
    printf("timings: %d/%d\n", tpass, HS_TIMING_VECTOR_COUNT);

    uint32_t small[10];
    int ok = hs_frame_to_timings(hs_vectors[0].frame, small, 10) == 0;
    printf("small buffer rejected: %s\n", ok ? "yes" : "no");
    return (pass == HS_VECTOR_COUNT && tpass == HS_TIMING_VECTOR_COUNT && ok) ? 0 : 1;
}
