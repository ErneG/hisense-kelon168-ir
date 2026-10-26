import json
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


def find_decoder():
    env = os.environ.get("IR_DECODER_DIR")
    candidates = []
    if env:
        candidates.append(Path(env).expanduser())
    candidates.append(REPO_ROOT.parent / "python-ir-remote-decoder")
    for c in candidates:
        if (c / "flipper_ir.py").is_file():
            return c
    sys.exit(
        "error: python-ir-remote-decoder not found.\n"
        "Set IR_DECODER_DIR to its checkout, or clone it next to this repo as\n"
        f"{REPO_ROOT.parent / 'python-ir-remote-decoder'}.\n"
        "Tried: " + ", ".join(str(c) for c in candidates)
    )


SRC = find_decoder()
sys.path.insert(0, str(SRC))
import flipper_ir as f

MODE = {"heat": "HsModeHeat", "cool": "HsModeCool", "dry": "HsModeDry", "fan": "HsModeFan"}
FAN = {"auto": "HsFanAuto", "high": "HsFanHigh", "midhigh": "HsFanMidHigh",
       "mid": "HsFanMid", "midlow": "HsFanMidLow", "low": "HsFanLow"}


def load():
    frames = []
    for line in (SRC / "captures/session_s1/session.jsonl").read_text().splitlines():
        r = json.loads(line)
        frames.append(("s1:" + r["step_id"], bytes.fromhex(r["hex"])))
    for p in sorted((SRC / "captures").glob("bits_smoke_fw151_*.txt")):
        frames.append((p.stem, bytes.fromhex(p.read_text().split()[1])))
    return frames


def arr(b):
    return "{" + ",".join(f"0x{x:02X}" for x in b) + "}"


def main(out):
    rows = []
    skipped = 0
    for name, d in load():
        st, cmd = f.parse_frame(d)
        if cmd in (5, 0x1D):
            skipped += 1
            continue
        exp = f.build_frame(st, cmd)
        assert exp == d, name
        rows.append((name, st, cmd, exp))
    lines = ["#pragma once", '#include "../hisense_codec.h"', "",
             "typedef struct {", "    const char* name;", "    hs_state_t state;",
             "    uint8_t cmd;", "    uint8_t frame[HS_FRAME_BYTES];", "} hs_vector_t;", "",
             f"#define HS_VECTOR_COUNT {len(rows)}", "",
             "static const hs_vector_t hs_vectors[HS_VECTOR_COUNT] = {"]
    for name, st, cmd, exp in rows:
        lines.append(
            f'    {{"{name}", {{{str(st.power).lower()}, {MODE[st.mode]}, {st.temp}, {FAN[st.fan]}, '
            f'{str(st.sleep).lower()}, {str(st.turbo).lower()}, {str(st.economy).lower()}, '
            f'{st.clock_h}, {st.clock_m}}}, 0x{exp[15]:02X}, {arr(exp)}}},')
    lines += ["};", "", "#define HS_TIMING_VECTOR_COUNT 3", "#define HS_TIMING_LEN %d" % len(f.encode(rows[0][3])), "",
              "static const uint32_t hs_timing_vectors[HS_TIMING_VECTOR_COUNT][HS_TIMING_LEN] = {"]
    for idx in (0, len(rows) // 2, len(rows) - 1):
        lines.append("    {" + ",".join(str(x) for x in f.encode(rows[idx][3])) + "},")
        lines.append("")
    lines[-1:] = []
    lines += ["};", "", "static const int hs_timing_vector_index[HS_TIMING_VECTOR_COUNT] = {%d, %d, %d};" %
              (0, len(rows) // 2, len(rows) - 1), "",
              f"#define HS_PY_CARRIER {f.FREQ}", f"#define HS_PY_DUTY {f.DUTY}", ""]
    Path(out).write_text("\n".join(lines))
    print(f"{len(rows)} vectors, skipped {skipped} timer frames")


main(sys.argv[1])
